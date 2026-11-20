// ff-bgm1x: 소리 분류 창. 설명은 ff_sound_catalog_dialog.h
#include <QComboBox>
#include <QDateTime>
#include <QEvent>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHideEvent>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "audio_core/hle/bgm_replacer.h"
#include "citra_qt/ff_sound_catalog_dialog.h"

using AudioCore::HLE::BgmReplacer;
using AudioCore::HLE::SoundCatalog;

FfSoundCatalogDialog* FfSoundCatalogDialog::current = nullptr;

namespace {

// 콤보 상자 항목 순서 = 분류 값
const int kKinds[] = {SoundCatalog::Kind_Auto,     SoundCatalog::Kind_Bgm, SoundCatalog::Kind_Jingle,
                      SoundCatalog::Kind_Cutscene, SoundCatalog::Kind_Sfx, SoundCatalog::Kind_None};

QString TypeName(int type) {
    switch (type) {
    case SoundCatalog::Type_Stream:
        return QStringLiteral("스트림 곡");
    case SoundCatalog::Type_Wave:
        return QStringLiteral("파형");
    }
    return QStringLiteral("?");
}

QString KindLabel(int kind) {
    return kind == SoundCatalog::Kind_Auto ? QStringLiteral("자동 (기본값)")
                                           : QString::fromUtf8(SoundCatalog::KindName(kind));
}

enum Column { Col_Order, Col_Type, Col_Name, Col_Auto, Col_User, Col_Volume, Col_Count, Col_Key, Col_Num };

SoundCatalog& Cat() {
    return BgmReplacer::Instance().Catalog();
}

} // namespace

void FfSoundCatalogDialog::Open(QWidget* parent, Hooks hooks) {
    if (current) {
        current->show();
        current->raise();
        current->activateWindow();
        return;
    }
    current = new FfSoundCatalogDialog(parent, std::move(hooks));
    current->show();
}

void FfSoundCatalogDialog::CloseIfOpen() {
    if (!current) return;
    FfSoundCatalogDialog* dlg = current;
    dlg->close();  // hideEvent: 재생 멈춤, 수집 끔, 우리가 건 일시정지 풀기
    current = nullptr;
    delete dlg;    // 본 창보다 먼저 지운다 (deleteLater 를 기다리지 않음)
}

FfSoundCatalogDialog::~FfSoundCatalogDialog() {
    if (current == this) current = nullptr;
}

FfSoundCatalogDialog::FfSoundCatalogDialog(QWidget* parent, Hooks hooks_)
    : QDialog(parent), hooks(std::move(hooks_)) {
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(QStringLiteral("소리 분류 (ff-bgm1x)"));
    resize(900, 520);

    QVBoxLayout* layout = new QVBoxLayout(this);
    status = new QLabel(this);
    status->setWordWrap(true);
    layout->addWidget(status);

    table = new QTableWidget(0, Col_Num, this);
    table->setHorizontalHeaderLabels({QStringLiteral("순서"), QStringLiteral("종류"), QStringLiteral("이름"),
                                      QStringLiteral("자동 판단"), QStringLiteral("지정"), QStringLiteral("볼륨"),
                                      QStringLiteral("나온 횟수"), QStringLiteral("이름표")});
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
    play_button = new QPushButton(QStringLiteral("재생"), this);
    QPushButton* stop_button = new QPushButton(QStringLiteral("중지"), this);
    kind_box = new QComboBox(this);
    for (int k : kKinds) kind_box->addItem(KindLabel(k));
    volume_box = new QSpinBox(this);
    volume_box->setRange(0, SoundCatalog::kMaxVolume);
    volume_box->setSuffix(QStringLiteral(" %"));
    volume_box->setValue(100);
    volume_box->setToolTip(QStringLiteral("이 소리의 볼륨 (100 = 그대로, 최대 200). 1배속으로 내는 소리와 원본 그대로 나오는 소리 모두에 적용됩니다."));
    QPushButton* reset_button = new QPushButton(QStringLiteral("기본값으로"), this);
    row->addWidget(play_button);
    row->addWidget(stop_button);
    row->addSpacing(16);
    row->addWidget(new QLabel(QStringLiteral("분류:"), this));
    row->addWidget(kind_box);
    row->addWidget(new QLabel(QStringLiteral("볼륨:"), this));
    row->addWidget(volume_box);
    row->addWidget(reset_button);
    row->addStretch(1);
    layout->addLayout(row);

    QHBoxLayout* row2 = new QHBoxLayout();
    QPushButton* import_button = new QPushButton(QStringLiteral("가져오기..."), this);
    QPushButton* export_button = new QPushButton(QStringLiteral("내보내기..."), this);
    QPushButton* clear_button = new QPushButton(QStringLiteral("목록 비우기"), this);
    row2->addWidget(import_button);
    row2->addWidget(export_button);
    row2->addStretch(1);
    row2->addWidget(clear_button);
    layout->addLayout(row2);

    // 재생/일시정지: 재생 중이면 일시정지, 같은 소리를 일시정지해 둔 상태면 이어서 재생, 그 밖에는 고른 소리를 처음부터
    connect(play_button, &QPushButton::clicked, this, [this] {
        auto& bgm = BgmReplacer::Instance();
        const QString key = SelectedKey();
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (bgm.PreviewPlaying() && !bgm.PreviewPaused()) {
            bgm.PreviewSetPaused(true);
            play_elapsed += (now - play_start_ms) / 1000.0;
            UpdatePlayButton();
            return;
        }
        if (bgm.PreviewPaused() && (key.isEmpty() || key == playing_key)) {
            bgm.PreviewSetPaused(false);
            play_start_ms = now;
            UpdatePlayButton();
            return;
        }
        if (key.isEmpty()) return;
        std::string err;
        if (!bgm.PreviewPlay(key.toStdString(), err)) {
            QMessageBox::warning(this, windowTitle(), QString::fromStdString(err));
            return;
        }
        const int r = table->currentRow();
        playing_name = table->item(r, Col_Name) ? table->item(r, Col_Name)->text() : key;
        playing_key = key;
        play_start_ms = now;
        play_elapsed = 0;
        play_length = bgm.PreviewLength();
        UpdatePlayButton();
    });
    connect(stop_button, &QPushButton::clicked, this, [this] {
        BgmReplacer::Instance().PreviewStop();
        UpdatePlayButton();
    });
    // 두 번 클릭: 지정 칸은 분류 고르기, 볼륨 칸은 볼륨 고치기, 그 밖의 칸은 재생
    connect(table, &QTableWidget::cellDoubleClicked, this, [this](int r, int col) {
        table->selectRow(r);
        const QString key = SelectedKey();
        if (key.isEmpty()) return;
        if (col == Col_User) {
            QMenu menu(this);
            const int now = Cat().UserKind(key.toStdString());
            for (int k : kKinds) {
                QAction* a = menu.addAction(KindLabel(k));
                a->setCheckable(true);
                a->setChecked(k == now);
                a->setData(k);
            }
            const QRect rect = table->visualItemRect(table->item(r, col));
            QAction* chosen = menu.exec(table->viewport()->mapToGlobal(rect.bottomLeft()));
            if (chosen) ApplyKind(chosen->data().toInt());
            return;
        }
        if (col == Col_Volume) {
            QSpinBox* box = new QSpinBox(table);
            box->setRange(0, SoundCatalog::kMaxVolume);
            box->setSuffix(QStringLiteral(" %"));
            box->setValue(Cat().Volume(key.toStdString()));
            editing = true;
            table->setCellWidget(r, col, box);
            box->setFocus();
            box->selectAll();
            connect(box, &QSpinBox::editingFinished, this, [this, box, key, r, col] {
                if (!editing) return;
                editing = false;
                Cat().SetVolume(key.toStdString(), box->value());
                table->removeCellWidget(r, col);
                Refresh(true);
            });
            return;
        }
        play_button->click();
    });
    connect(kind_box, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (updating_ui || i < 0) return;
        ApplyKind(kKinds[i]);
    });
    connect(volume_box, &QSpinBox::valueChanged, this, [this](int v) {
        if (updating_ui) return;
        const QString key = SelectedKey();
        if (key.isEmpty()) return;
        Cat().SetVolume(key.toStdString(), v);
        Refresh(true);
    });
    connect(reset_button, &QPushButton::clicked, this, [this] {
        const QString key = SelectedKey();
        if (key.isEmpty()) return;
        Cat().ResetUser(key.toStdString());
        Refresh(true);
        updating_ui = true;
        kind_box->setCurrentIndex(0);
        volume_box->setValue(100);
        updating_ui = false;
    });
    connect(table, &QTableWidget::itemSelectionChanged, this, [this] {
        const QString key = SelectedKey();
        const int user = key.isEmpty() ? SoundCatalog::Kind_Auto : Cat().UserKind(key.toStdString());
        updating_ui = true;
        for (int i = 0; i < static_cast<int>(std::size(kKinds)); i++) {
            if (kKinds[i] == user) kind_box->setCurrentIndex(i);
        }
        volume_box->setValue(key.isEmpty() ? 100 : Cat().Volume(key.toStdString()));
        updating_ui = false;
    });
    connect(import_button, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("소리 분류 가져오기"), QString(),
                                                          QStringLiteral("소리 분류 (*.txt);;모든 파일 (*)"));
        if (path.isEmpty()) return;
        std::string err;
        if (!Cat().ImportFrom(path.toStdString(), err)) {
            QMessageBox::warning(this, windowTitle(), QString::fromStdString(err));
        }
        Refresh(true);
    });
    connect(export_button, &QPushButton::clicked, this, [this] {
        const std::string code = Cat().GameCode();
        const QString name = QString::fromStdString(code.empty() ? std::string("sounds") : code) +
                             QStringLiteral(".txt");
        const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("소리 분류 내보내기"), name,
                                                          QStringLiteral("소리 분류 (*.txt)"));
        if (path.isEmpty()) return;
        if (!Cat().ExportTo(path.toStdString())) {
            QMessageBox::warning(this, windowTitle(), QStringLiteral("파일을 쓸 수 없습니다."));
        }
    });
    connect(clear_button, &QPushButton::clicked, this, [this] {
        Cat().ClearSeen();
        Refresh(true);
    });

    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this] {
        Refresh(false);
        // 재생 중인 소리와 경과 시간
        UpdatePlayButton();
        auto& bgm = BgmReplacer::Instance();
        if (bgm.PreviewPlaying()) {
            const bool paused = bgm.PreviewPaused();
            const double sec =
                play_elapsed + (paused ? 0 : (QDateTime::currentMSecsSinceEpoch() - play_start_ms) / 1000.0);
            QString t = (paused ? QStringLiteral("일시정지: %1 — %2초") : QStringLiteral("재생 중: %1 — %2초"))
                            .arg(playing_name)
                            .arg(sec, 0, 'f', 1);
            if (play_length > 0) t += QStringLiteral(" / %1초").arg(play_length, 0, 'f', 1);
            playing->setText(t);
        } else {
            playing->setText(playing_name.isEmpty() ? QString()
                                                    : QStringLiteral("재생 끝: %1").arg(playing_name));
        }
    });
    timer->start(300);

    Cat().SetCollecting(true);
    Refresh(true);
    UpdateMode();
}

void FfSoundCatalogDialog::UpdatePlayButton() {
    auto& bgm = BgmReplacer::Instance();
    const QString text = bgm.PreviewPlaying() && !bgm.PreviewPaused() ? QStringLiteral("일시정지")
                                                                      : QStringLiteral("재생");
    if (play_button->text() != text) play_button->setText(text);
}

QString FfSoundCatalogDialog::SelectedKey() const {
    const int r = table->currentRow();
    if (r < 0) return {};
    QTableWidgetItem* it = table->item(r, Col_Key);
    return it ? it->text() : QString();
}

void FfSoundCatalogDialog::ApplyKind(int kind) {
    const QString key = SelectedKey();
    if (key.isEmpty()) return;
    Cat().SetUserKind(key.toStdString(), kind);
    Refresh(true);
}

void FfSoundCatalogDialog::Refresh(bool force) {
    const unsigned v = Cat().SeenVersion();
    if (editing) return;  // 볼륨 칸을 고치는 중에는 목록을 다시 그리지 않는다
    if (!force && v == shown_version) return;
    shown_version = v;
    const QString keep = SelectedKey();
    const std::vector<SoundCatalog::Seen> list = Cat().SeenList();
    table->setSortingEnabled(false);
    table->setRowCount(static_cast<int>(list.size()));
    for (int r = 0; r < static_cast<int>(list.size()); r++) {
        const SoundCatalog::Seen& s = list[r];
        auto item = [&](int col) {
            QTableWidgetItem* it = table->item(r, col);
            if (!it) {
                it = new QTableWidgetItem();
                table->setItem(r, col, it);
            }
            return it;
        };
        item(Col_Order)->setData(Qt::DisplayRole, static_cast<int>(s.order) + 1);
        item(Col_Type)->setText(TypeName(s.type));
        item(Col_Name)->setText(QString::fromStdString(s.name));
        item(Col_Auto)->setText(QString::fromUtf8(SoundCatalog::KindName(s.auto_kind)));
        item(Col_User)->setText(s.user_kind == SoundCatalog::Kind_Auto
                                    ? QStringLiteral("-")
                                    : QString::fromUtf8(SoundCatalog::KindName(s.user_kind)));
        item(Col_Volume)->setText(QStringLiteral("%1 %").arg(s.volume));
        item(Col_Count)->setData(Qt::DisplayRole, s.count);
        item(Col_Key)->setText(QString::fromStdString(s.key));
    }
    table->setSortingEnabled(true);
    if (!keep.isEmpty()) {
        for (int r = 0; r < table->rowCount(); r++) {
            if (table->item(r, Col_Key) && table->item(r, Col_Key)->text() == keep) table->selectRow(r);
        }
    }
}

void FfSoundCatalogDialog::UpdateMode() {
    const std::string code = Cat().GameCode();
    const QString game = code.empty() ? QStringLiteral("(게임 없음)")
                                      : QString::fromStdString(Cat().GameTitle() + " [" + code + "]");
    if (isActiveWindow()) {
        status->setText(game + QStringLiteral(" — 일시정지 중. 에뮬레이터 창을 누르면 게임이 진행되며 나오는 소리를 모읍니다.\n"
                                              "분류는 바로 저장됩니다: ") +
                        QString::fromStdString(Cat().StoragePath()));
    } else {
        status->setText(game + QStringLiteral(" — 수집 중. 이 창을 누르면 게임이 일시정지됩니다."));
    }
}

void FfSoundCatalogDialog::PauseGame() {
    if (paused_by_us || !hooks.is_running || !hooks.is_running()) return;
    hooks.pause();
    // 1배속 BGM·효과음도 멈춘다 (재생 버튼 소리만 들리게)
    BgmReplacer::Instance().SetEmulationRunning(false);
    paused_by_us = true;
}

void FfSoundCatalogDialog::ResumeGame() {
    if (!paused_by_us) return;
    paused_by_us = false;
    BgmReplacer::Instance().SetEmulationRunning(true);
    hooks.resume();
}

void FfSoundCatalogDialog::changeEvent(QEvent* event) {
    QDialog::changeEvent(event);
    if (event->type() != QEvent::ActivationChange || closing || !isVisible()) return;
    // 이 창이 앞에 오면 게임을 멈추고, 에뮬레이터 창으로 가면 다시 돌린다 (우리가 건 일시정지만 푼다)
    if (isActiveWindow()) {
        PauseGame();
    } else if (paused_by_us) {
        BgmReplacer::Instance().PreviewStop();
        ResumeGame();
    }
    UpdateMode();
}

// 닫기 버튼, Esc, 창 닫기 모두 여기로 온다
void FfSoundCatalogDialog::hideEvent(QHideEvent* event) {
    closing = true;
    BgmReplacer::Instance().PreviewStop();
    Cat().SetCollecting(false);
    ResumeGame();
    QDialog::hideEvent(event);
    // Esc 로 닫으면 숨기기만 하므로 지운다 (다음에 열 때 새로 만든다)
    deleteLater();
}
