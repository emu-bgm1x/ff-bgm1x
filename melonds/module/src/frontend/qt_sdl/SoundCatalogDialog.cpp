// ff-bgm1x: 소리 분류 창. 설명은 SoundCatalogDialog.h
#include "SoundCatalogDialog.h"

#include <QDateTime>
#include <QHideEvent>
#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QMenu>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "BgmReplacer.h"
#include "EmuInstance.h"
#include "EmuThread.h"

SoundCatalogDialog* SoundCatalogDialog::current = nullptr;

namespace
{

// 콤보 상자 항목 순서 = 분류 값
const int kKinds[] = {SoundCatalog::Kind_Auto, SoundCatalog::Kind_Bgm, SoundCatalog::Kind_Jingle,
                      SoundCatalog::Kind_Cutscene, SoundCatalog::Kind_Sfx, SoundCatalog::Kind_None};

QString typeName(int type)
{
    switch (type)
    {
    case SoundCatalog::Type_Seq: return "시퀀스";
    case SoundCatalog::Type_Ssar: return "효과음 묶음";
    case SoundCatalog::Type_Strm: return "스트림(STRM)";
    }
    return "?";
}

enum Column { Col_Order, Col_Type, Col_Name, Col_Auto, Col_User, Col_Volume, Col_Count, Col_Key, Col_Num };

} // namespace

SoundCatalogDialog* SoundCatalogDialog::openDlg(QWidget* parent, EmuInstance* inst)
{
    if (current)
    {
        current->show();
        current->raise();
        current->activateWindow();
        return current;
    }
    current = new SoundCatalogDialog(parent, inst);
    current->show();
    return current;
}

SoundCatalogDialog::SoundCatalogDialog(QWidget* parent, EmuInstance* inst)
    : QDialog(parent), inst(inst), thread(inst->getEmuThread())
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle("소리 분류 (ff-bgm1x)");
    resize(860, 520);

    QVBoxLayout* layout = new QVBoxLayout(this);
    status = new QLabel(this);
    status->setWordWrap(true);
    layout->addWidget(status);

    table = new QTableWidget(0, Col_Num, this);
    table->setHorizontalHeaderLabels({"순서", "종류", "이름", "자동 판단", "지정", "볼륨", "나온 횟수", "이름표"});
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->verticalHeader()->setVisible(false);
    // 칸 너비는 모두 손으로 조절한다 (한 칸만 창에 맞춰 늘리면 다른 칸을 넓힐 때 그 칸이 대신 줄어든다).
    // 남는 자리는 마지막 칸(이름표)이 채운다
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table->horizontalHeader()->setStretchLastSection(true);
    table->setColumnWidth(Col_Name, 300);
    table->setSortingEnabled(true);
    layout->addWidget(table, 1);

    playing = new QLabel(this);
    layout->addWidget(playing);

    QHBoxLayout* row = new QHBoxLayout();
    playButton = new QPushButton("재생", this);
    QPushButton* stopButton = new QPushButton("중지", this);
    kindBox = new QComboBox(this);
    for (int k : kKinds)
        kindBox->addItem(k == SoundCatalog::Kind_Auto ? QString("자동 (기본값)") : QString(SoundCatalog::kindName(k)));
    volumeBox = new QSpinBox(this);
    volumeBox->setRange(0, SoundCatalog::kMaxVolume);
    volumeBox->setSuffix(" %");
    volumeBox->setValue(100);
    volumeBox->setToolTip("이 소리의 볼륨 (100 = 그대로, 최대 200). 1배속으로 내는 소리와 원본 그대로 나오는 소리 모두에 적용됩니다.");
    QPushButton* resetButton = new QPushButton("기본값으로", this);
    row->addWidget(playButton);
    row->addWidget(stopButton);
    row->addSpacing(16);
    row->addWidget(new QLabel("분류:", this));
    row->addWidget(kindBox);
    row->addWidget(new QLabel("볼륨:", this));
    row->addWidget(volumeBox);
    row->addWidget(resetButton);
    row->addStretch(1);
    layout->addLayout(row);

    QHBoxLayout* row2 = new QHBoxLayout();
    QPushButton* importButton = new QPushButton("가져오기...", this);
    QPushButton* exportButton = new QPushButton("내보내기...", this);
    QPushButton* clearButton = new QPushButton("목록 비우기", this);
    row2->addWidget(importButton);
    row2->addWidget(exportButton);
    row2->addStretch(1);
    row2->addWidget(clearButton);
    layout->addLayout(row2);

    SoundCatalog& cat = inst->bgmReplacer->catalog();

    // 재생/일시정지: 재생 중이면 일시정지, 같은 소리를 일시정지해 둔 상태면 이어서 재생, 그 밖에는 고른 소리를 처음부터
    connect(playButton, &QPushButton::clicked, this, [this, &cat] {
        const QString key = selectedKey();
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (preview.isPlaying() && !preview.isPaused())
        {
            preview.setPaused(true);
            playElapsed += (now - playStartMs) / 1000.0;
            updatePlayButton();
            return;
        }
        if (preview.isPlaying() && preview.isPaused() && (key.isEmpty() || key == playingKey))
        {
            preview.setPaused(false);
            playStartMs = now;
            updatePlayButton();
            return;
        }
        if (key.isEmpty()) return;
        std::string err;
        if (!preview.play(cat, key.toStdString(), err))
        {
            QMessageBox::warning(this, windowTitle(), QString::fromStdString(err));
            return;
        }
        const int r = table->currentRow();
        playingName = table->item(r, Col_Name) ? table->item(r, Col_Name)->text() : key;
        playingKey = key;
        playStartMs = now;
        playElapsed = 0;
        playLength = preview.lengthSeconds();
        updatePlayButton();
    });
    connect(stopButton, &QPushButton::clicked, this, [this] {
        preview.stop();
        updatePlayButton();
    });
    // 두 번 클릭: 지정 칸은 분류 고르기, 볼륨 칸은 볼륨 고치기, 그 밖의 칸은 재생
    connect(table, &QTableWidget::cellDoubleClicked, this, [this](int row, int col) {
        table->selectRow(row);
        const QString key = selectedKey();
        if (key.isEmpty()) return;
        SoundCatalog& c = this->inst->bgmReplacer->catalog();
        if (col == Col_User)
        {
            QMenu menu(this);
            const int now = c.userKind(key.toStdString());
            for (int k : kKinds)
            {
                QAction* a = menu.addAction(k == SoundCatalog::Kind_Auto ? QString("자동 (기본값)") : QString(SoundCatalog::kindName(k)));
                a->setCheckable(true);
                a->setChecked(k == now);
                a->setData(k);
            }
            const QRect r = table->visualItemRect(table->item(row, col));
            QAction* chosen = menu.exec(table->viewport()->mapToGlobal(r.bottomLeft()));
            if (chosen) applyKind(chosen->data().toInt());
            return;
        }
        if (col == Col_Volume)
        {
            QSpinBox* box = new QSpinBox(table);
            box->setRange(0, SoundCatalog::kMaxVolume);
            box->setSuffix(" %");
            box->setValue(c.volume(key.toStdString()));
            editing = true;
            table->setCellWidget(row, col, box);
            box->setFocus();
            box->selectAll();
            connect(box, &QSpinBox::editingFinished, this, [this, box, key, row, col] {
                if (!editing) return;
                editing = false;
                this->inst->bgmReplacer->catalog().setVolume(key.toStdString(), box->value());
                table->removeCellWidget(row, col);
                refresh(true);
            });
            return;
        }
        playButton->click();
    });
    connect(kindBox, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (updatingUi || i < 0) return;
        applyKind(kKinds[i]);
    });
    connect(volumeBox, &QSpinBox::valueChanged, this, [this](int v) {
        if (updatingUi) return;
        const QString key = selectedKey();
        if (key.isEmpty()) return;
        this->inst->bgmReplacer->catalog().setVolume(key.toStdString(), v);
        refresh(true);
    });
    connect(resetButton, &QPushButton::clicked, this, [this] {
        const QString key = selectedKey();
        if (key.isEmpty()) return;
        this->inst->bgmReplacer->catalog().resetUser(key.toStdString());
        refresh(true);
        updatingUi = true;
        kindBox->setCurrentIndex(0);
        volumeBox->setValue(100);
        updatingUi = false;
    });
    connect(table, &QTableWidget::itemSelectionChanged, this, [this, &cat] {
        const QString key = selectedKey();
        const int user = key.isEmpty() ? SoundCatalog::Kind_Auto : cat.userKind(key.toStdString());
        updatingUi = true;
        for (int i = 0; i < (int)(sizeof(kKinds) / sizeof(kKinds[0])); i++)
            if (kKinds[i] == user) kindBox->setCurrentIndex(i);
        volumeBox->setValue(key.isEmpty() ? 100 : cat.volume(key.toStdString()));
        updatingUi = false;
    });
    connect(importButton, &QPushButton::clicked, this, [this, &cat] {
        const QString path = QFileDialog::getOpenFileName(this, "소리 분류 가져오기", QString(), "소리 분류 (*.txt);;모든 파일 (*)");
        if (path.isEmpty()) return;
        std::string err;
        if (!cat.importFrom(path.toStdString(), err))
            QMessageBox::warning(this, windowTitle(), QString::fromStdString(err));
        refresh(true);
    });
    connect(exportButton, &QPushButton::clicked, this, [this, &cat] {
        const QString name = QString::fromStdString(cat.gameCode().empty() ? std::string("sounds") : cat.gameCode()) + ".txt";
        const QString path = QFileDialog::getSaveFileName(this, "소리 분류 내보내기", name, "소리 분류 (*.txt)");
        if (path.isEmpty()) return;
        if (!cat.exportTo(path.toStdString()))
            QMessageBox::warning(this, windowTitle(), "파일을 쓸 수 없습니다.");
    });
    connect(clearButton, &QPushButton::clicked, this, [this, &cat] {
        cat.clearSeen();
        refresh(true);
    });

    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this] {
        refresh(false);
        // 재생 중인 소리와 경과 시간
        updatePlayButton();
        if (preview.isPlaying())
        {
            const bool paused = preview.isPaused();
            const double sec = playElapsed + (paused ? 0 : (QDateTime::currentMSecsSinceEpoch() - playStartMs) / 1000.0);
            QString t = QString(paused ? "일시정지: %1 — %2초" : "재생 중: %1 — %2초").arg(playingName).arg(sec, 0, 'f', 1);
            if (playLength > 0) t += QString(" / %1초").arg(playLength, 0, 'f', 1);
            playing->setText(t);
        }
        else
            playing->setText(playingName.isEmpty() ? QString() : QString("재생 끝: %1").arg(playingName));
    });
    timer->start(300);

    cat.setCollecting(true);
    refresh(true);
    updateMode();
}

void SoundCatalogDialog::closeFor(EmuInstance* inst)
{
    if (!current || current->inst != inst) return;
    SoundCatalogDialog* dlg = current;
    dlg->close();  // hideEvent: 재생 멈춤, 수집 끔, 우리가 건 일시정지 풀기
    current = nullptr;
    delete dlg;    // 에뮬레이터보다 먼저 지운다 (deleteLater 를 기다리지 않음)
}

SoundCatalogDialog::~SoundCatalogDialog()
{
    if (current == this) current = nullptr;
}

void SoundCatalogDialog::updatePlayButton()
{
    const QString text = preview.isPlaying() && !preview.isPaused() ? QString("일시정지") : QString("재생");
    if (playButton->text() != text) playButton->setText(text);
}

QString SoundCatalogDialog::selectedKey() const
{
    const int r = table->currentRow();
    if (r < 0) return {};
    QTableWidgetItem* it = table->item(r, Col_Key);
    return it ? it->text() : QString();
}

void SoundCatalogDialog::applyKind(int kind)
{
    const QString key = selectedKey();
    if (key.isEmpty()) return;
    inst->bgmReplacer->catalog().setUserKind(key.toStdString(), kind);
    refresh(true);
}

void SoundCatalogDialog::refresh(bool force)
{
    SoundCatalog& cat = inst->bgmReplacer->catalog();
    const unsigned v = cat.seenVersion();
    if (editing) return;  // 볼륨 칸을 고치는 중에는 목록을 다시 그리지 않는다
    if (!force && v == shownVersion) return;
    shownVersion = v;
    const QString keep = selectedKey();
    const std::vector<SoundCatalog::Seen> list = cat.seenList();
    table->setSortingEnabled(false);
    table->setRowCount((int)list.size());
    int select = -1;
    for (int r = 0; r < (int)list.size(); r++)
    {
        const SoundCatalog::Seen& s = list[r];
        auto set = [&](int col, const QString& text) {
            QTableWidgetItem* it = table->item(r, col);
            if (!it)
            {
                it = new QTableWidgetItem();
                table->setItem(r, col, it);
            }
            it->setText(text);
        };
        QTableWidgetItem* orderItem = table->item(r, Col_Order);
        if (!orderItem)
        {
            orderItem = new QTableWidgetItem();
            table->setItem(r, Col_Order, orderItem);
        }
        orderItem->setData(Qt::DisplayRole, (int)s.order + 1);
        set(Col_Type, typeName(s.type));
        set(Col_Name, QString::fromStdString(s.name));
        set(Col_Auto, SoundCatalog::kindName(s.autoKind));
        set(Col_User, s.userKind == SoundCatalog::Kind_Auto ? QString("-") : QString(SoundCatalog::kindName(s.userKind)));
        set(Col_Volume, QString("%1 %").arg(s.volume));
        QTableWidgetItem* countItem = table->item(r, Col_Count);
        if (!countItem)
        {
            countItem = new QTableWidgetItem();
            table->setItem(r, Col_Count, countItem);
        }
        countItem->setData(Qt::DisplayRole, s.count);
        set(Col_Key, QString::fromStdString(s.key));
        if (QString::fromStdString(s.key) == keep) select = r;
    }
    table->setSortingEnabled(true);
    if (select >= 0)
    {
        for (int r = 0; r < table->rowCount(); r++)
            if (table->item(r, Col_Key) && table->item(r, Col_Key)->text() == keep) table->selectRow(r);
    }
}

void SoundCatalogDialog::updateMode()
{
    SoundCatalog& cat = inst->bgmReplacer->catalog();
    const QString game = cat.gameCode().empty() ? QString("(게임 없음)")
                                                 : QString::fromStdString(cat.gameTitle() + " [" + cat.gameCode() + "]");
    if (isActiveWindow())
        status->setText(game + " — 일시정지 중. 에뮬레이터 창을 누르면 게임이 진행되며 나오는 소리를 모읍니다.\n"
                               "분류는 바로 저장됩니다: " + QString::fromStdString(cat.storagePath()));
    else
        status->setText(game + " — 수집 중. 이 창을 누르면 게임이 일시정지됩니다.");
}

void SoundCatalogDialog::changeEvent(QEvent* event)
{
    QDialog::changeEvent(event);
    if (event->type() != QEvent::ActivationChange || closing || !isVisible()) return;
    // 이 창이 앞에 오면 게임을 멈추고, 에뮬레이터 창으로 가면 다시 돌린다 (우리가 건 일시정지만 푼다)
    if (isActiveWindow() && !pausedByUs)
    {
        thread->emuPause();
        pausedByUs = true;
    }
    else if (!isActiveWindow() && pausedByUs)
    {
        preview.stop();
        thread->emuUnpause();
        pausedByUs = false;
    }
    updateMode();
}

// 닫기 버튼, Esc, 창 닫기 모두 여기로 온다
void SoundCatalogDialog::hideEvent(QHideEvent* event)
{
    closing = true;
    preview.stop();
    inst->bgmReplacer->catalog().setCollecting(false);
    if (pausedByUs)
    {
        thread->emuUnpause();
        pausedByUs = false;
    }
    QDialog::hideEvent(event);
    // Esc 로 닫으면 숨기기만 하므로 지운다 (다음에 열 때 새로 만든다)
    deleteLater();
}
