/* ff-bgm1x: 소리 분류 창. 설명은 FfSoundCatalogDialog.h */
#include "FfSoundCatalogDialog.h"

#include <iterator>

#include <QComboBox>
#include <QDateTime>
#include <QEvent>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHideEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "BgmReplacer.h"
#include "sound_catalog.h"

using namespace QGBA;

FfSoundCatalogDialog* FfSoundCatalogDialog::s_current = nullptr;

namespace {

const int kKinds[] = {SoundCatalog::Kind_Auto, SoundCatalog::Kind_Bgm, SoundCatalog::Kind_Jingle,
                      SoundCatalog::Kind_Cutscene, SoundCatalog::Kind_Sfx, SoundCatalog::Kind_None};

QString typeName(int type) {
	switch (type) {
	case SoundCatalog::Type_Song:
		return QString::fromUtf8("곡");
	case SoundCatalog::Type_Cry:
		return QString::fromUtf8("울음소리");
	case SoundCatalog::Type_Sfx:
		return QString::fromUtf8("효과음");
	}
	return QString::fromUtf8("기타");
}

QString kindLabel(int kind) {
	return kind == SoundCatalog::Kind_Auto ? QString::fromUtf8("자동 (기본값)") : QString::fromUtf8(SoundCatalog::KindName(kind));
}

enum Column { Col_Order, Col_Type, Col_Name, Col_Auto, Col_User, Col_Volume, Col_Count, Col_Key, Col_Num };

}

void FfSoundCatalogDialog::open(QWidget* parent, Hooks hooks) {
	if (s_current) {
		s_current->show();
		s_current->raise();
		s_current->activateWindow();
		return;
	}
	s_current = new FfSoundCatalogDialog(parent, std::move(hooks));
	s_current->show();
}

void FfSoundCatalogDialog::closeIfOpen() {
	if (!s_current) {
		return;
	}
	FfSoundCatalogDialog* dlg = s_current;
	dlg->close();
	s_current = nullptr;
	delete dlg;
}

FfSoundCatalogDialog::~FfSoundCatalogDialog() {
	if (s_current == this) {
		s_current = nullptr;
	}
}

SoundCatalog* FfSoundCatalogDialog::catalog() {
	BgmReplacer* bgm = m_hooks.bgm ? m_hooks.bgm() : nullptr;
	SoundCatalog* cat = bgm ? &bgm->catalog() : nullptr;
	if (cat != m_shownCatalog) {
		// 게임이 바뀌었다: 새 게임에서 수집을 켜고, 재생 중인 소리는 멈춘다
		m_preview.stop();
		m_shownCatalog = cat;
		m_shownVersion = ~0u;
	}
	// 새 게임(같은 주소에 다시 만들어진 경우 포함)도 창이 열려 있는 동안은 수집한다
	if (cat && !m_closing && !cat->Collecting()) {
		cat->SetCollecting(true);
	}
	return cat;
}

FfSoundCatalogDialog::FfSoundCatalogDialog(QWidget* parent, Hooks hooks)
	: QDialog(parent)
	, m_hooks(std::move(hooks))
{
	setAttribute(Qt::WA_DeleteOnClose);
	setWindowTitle(QString::fromUtf8("소리 분류 (ff-bgm1x)"));
	resize(900, 520);

	QVBoxLayout* layout = new QVBoxLayout(this);
	m_status = new QLabel(this);
	m_status->setWordWrap(true);
	layout->addWidget(m_status);

	m_table = new QTableWidget(0, Col_Num, this);
	m_table->setHorizontalHeaderLabels({QString::fromUtf8("순서"), QString::fromUtf8("종류"), QString::fromUtf8("이름"),
	                                    QString::fromUtf8("자동 판단"), QString::fromUtf8("지정"), QString::fromUtf8("볼륨"),
	                                    QString::fromUtf8("나온 횟수"), QString::fromUtf8("이름표")});
	m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_table->setSelectionMode(QAbstractItemView::SingleSelection);
	m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_table->verticalHeader()->setVisible(false);
	// 칸 너비는 모두 손으로 조절한다. 남는 자리는 마지막 칸(이름표)이 채운다
	m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
	m_table->horizontalHeader()->setStretchLastSection(true);
	m_table->setColumnWidth(Col_Name, 300);
	m_table->setSortingEnabled(true);
	layout->addWidget(m_table, 1);

	m_playing = new QLabel(this);
	layout->addWidget(m_playing);

	QHBoxLayout* row = new QHBoxLayout();
	m_playButton = new QPushButton(QString::fromUtf8("재생"), this);
	QPushButton* stopButton = new QPushButton(QString::fromUtf8("중지"), this);
	m_kindBox = new QComboBox(this);
	for (int k : kKinds) {
		m_kindBox->addItem(kindLabel(k));
	}
	m_volumeBox = new QSpinBox(this);
	m_volumeBox->setRange(0, SoundCatalog::kMaxVolume);
	m_volumeBox->setSuffix(QString::fromUtf8(" %"));
	m_volumeBox->setValue(100);
	m_volumeBox->setToolTip(QString::fromUtf8("이 소리의 볼륨 (100 = 그대로, 최대 200). 1배속으로 내는 소리와 원본 그대로 나오는 소리 모두에 적용됩니다."));
	QPushButton* resetButton = new QPushButton(QString::fromUtf8("기본값으로"), this);
	row->addWidget(m_playButton);
	row->addWidget(stopButton);
	row->addSpacing(16);
	row->addWidget(new QLabel(QString::fromUtf8("분류:"), this));
	row->addWidget(m_kindBox);
	row->addWidget(new QLabel(QString::fromUtf8("볼륨:"), this));
	row->addWidget(m_volumeBox);
	row->addWidget(resetButton);
	row->addStretch(1);
	layout->addLayout(row);

	QHBoxLayout* row2 = new QHBoxLayout();
	QPushButton* importButton = new QPushButton(QString::fromUtf8("가져오기..."), this);
	QPushButton* exportButton = new QPushButton(QString::fromUtf8("내보내기..."), this);
	QPushButton* clearButton = new QPushButton(QString::fromUtf8("목록 비우기"), this);
	row2->addWidget(importButton);
	row2->addWidget(exportButton);
	row2->addStretch(1);
	row2->addWidget(clearButton);
	layout->addLayout(row2);

	connect(m_playButton, &QPushButton::clicked, this, [this]() {
		const QString key = selectedKey();
		const qint64 now = QDateTime::currentMSecsSinceEpoch();
		if (m_preview.isPlaying() && !m_preview.isPaused()) {
			m_preview.setPaused(true);
			m_playElapsed += (now - m_playStartMs) / 1000.0;
			updatePlayButton();
			return;
		}
		if (m_preview.isPaused() && (key.isEmpty() || key == m_playingKey)) {
			m_preview.setPaused(false);
			m_playStartMs = now;
			updatePlayButton();
			return;
		}
		if (key.isEmpty()) {
			return;
		}
		BgmReplacer* bgm = m_hooks.bgm ? m_hooks.bgm() : nullptr;
		std::string err;
		if (bgm && bgm->gbDriver()) {
			int cryId = -1, cryPitch = 0, cryLength = 0;
			bgm->gbDriver()->lastCry(cryId, cryPitch, cryLength);
			if (m_preview.playGb(bgm->gbDriver()->rom(), bgm->gbDriver()->profile().tables, key.toStdString(), err,
			                     cryId, cryPitch, cryLength)) {
				err.clear();
			}
		} else if (!bgm || !bgm->isGba()) {
			err = "게임이 실행 중일 때 재생할 수 있습니다.";
		} else if (m_preview.play(bgm->rom(), bgm->liveMirror(), key.toStdString(), err)) {
			err.clear();
		}
		if (!err.empty()) {
			QMessageBox::warning(this, windowTitle(), QString::fromStdString(err));
			return;
		}
		const int r = m_table->currentRow();
		m_playingName = m_table->item(r, Col_Name) ? m_table->item(r, Col_Name)->text() : key;
		m_playingKey = key;
		m_playStartMs = now;
		m_playElapsed = 0;
		m_playLength = m_preview.lengthSeconds();
		updatePlayButton();
	});
	connect(stopButton, &QPushButton::clicked, this, [this]() {
		m_preview.stop();
		updatePlayButton();
	});
	connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int r, int col) {
		m_table->selectRow(r);
		const QString key = selectedKey();
		SoundCatalog* cat = catalog();
		if (key.isEmpty() || !cat) {
			return;
		}
		if (col == Col_User) {
			QMenu menu(this);
			const int now = cat->UserKind(key.toStdString());
			for (int k : kKinds) {
				QAction* a = menu.addAction(kindLabel(k));
				a->setCheckable(true);
				a->setChecked(k == now);
				a->setData(k);
			}
			const QRect rect = m_table->visualItemRect(m_table->item(r, col));
			QAction* chosen = menu.exec(m_table->viewport()->mapToGlobal(rect.bottomLeft()));
			if (chosen) {
				applyKind(chosen->data().toInt());
			}
			return;
		}
		if (col == Col_Name) {
			// 이름 바꾸기 (GBA 곡은 롬에 이름이 없다). 비우면 원래 이름으로 돌아간다
			QLineEdit* edit = new QLineEdit(m_table);
			const QString label = QString::fromStdString(cat->Label(key.toStdString()));
			edit->setText(label.isEmpty() ? m_table->item(r, col)->text() : label);
			edit->setPlaceholderText(QString::fromUtf8("비우면 원래 이름"));
			m_editing = true;
			m_table->setCellWidget(r, col, edit);
			edit->setFocus();
			edit->selectAll();
			const QString before = edit->text();
			connect(edit, &QLineEdit::editingFinished, this, [this, edit, key, r, col, before, label]() {
				if (!m_editing) {
					return;
				}
				m_editing = false;
				const QString text = edit->text().trimmed();
				// 아무것도 안 바꾸고 나가면 그대로 둔다
				if (text != before || (text.isEmpty() && !label.isEmpty())) {
					if (SoundCatalog* c = catalog()) {
						c->SetLabel(key.toStdString(), text.toStdString());
					}
				}
				m_table->removeCellWidget(r, col);
				refresh(true);
			});
			return;
		}
		if (col == Col_Volume) {
			QSpinBox* box = new QSpinBox(m_table);
			box->setRange(0, SoundCatalog::kMaxVolume);
			box->setSuffix(QString::fromUtf8(" %"));
			box->setValue(cat->Volume(key.toStdString()));
			m_editing = true;
			m_table->setCellWidget(r, col, box);
			box->setFocus();
			box->selectAll();
			connect(box, &QSpinBox::editingFinished, this, [this, box, key, r, col]() {
				if (!m_editing) {
					return;
				}
				m_editing = false;
				if (SoundCatalog* c = catalog()) {
					c->SetVolume(key.toStdString(), box->value());
				}
				m_table->removeCellWidget(r, col);
				refresh(true);
			});
			return;
		}
		m_playButton->click();
	});
	connect(m_kindBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
		if (m_updatingUi || i < 0) {
			return;
		}
		applyKind(kKinds[i]);
	});
	connect(m_volumeBox, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
		if (m_updatingUi) {
			return;
		}
		const QString key = selectedKey();
		SoundCatalog* cat = catalog();
		if (key.isEmpty() || !cat) {
			return;
		}
		cat->SetVolume(key.toStdString(), v);
		refresh(true);
	});
	connect(resetButton, &QPushButton::clicked, this, [this]() {
		const QString key = selectedKey();
		SoundCatalog* cat = catalog();
		if (key.isEmpty() || !cat) {
			return;
		}
		cat->ResetUser(key.toStdString());
		refresh(true);
		m_updatingUi = true;
		m_kindBox->setCurrentIndex(0);
		m_volumeBox->setValue(100);
		m_updatingUi = false;
	});
	connect(m_table, &QTableWidget::itemSelectionChanged, this, [this]() {
		const QString key = selectedKey();
		SoundCatalog* cat = catalog();
		const int user = key.isEmpty() || !cat ? SoundCatalog::Kind_Auto : cat->UserKind(key.toStdString());
		m_updatingUi = true;
		for (int i = 0; i < (int) (sizeof(kKinds) / sizeof(kKinds[0])); i++) {
			if (kKinds[i] == user) {
				m_kindBox->setCurrentIndex(i);
			}
		}
		m_volumeBox->setValue(key.isEmpty() || !cat ? 100 : cat->Volume(key.toStdString()));
		m_updatingUi = false;
	});
	connect(importButton, &QPushButton::clicked, this, [this]() {
		SoundCatalog* cat = catalog();
		if (!cat) {
			return;
		}
		const QString path = QFileDialog::getOpenFileName(this, QString::fromUtf8("소리 분류 가져오기"), QString(),
		                                                  QString::fromUtf8("소리 분류 (*.txt);;모든 파일 (*)"));
		if (path.isEmpty()) {
			return;
		}
		std::string err;
		if (!cat->ImportFrom(path.toStdString(), err)) {
			QMessageBox::warning(this, windowTitle(), QString::fromStdString(err));
		}
		refresh(true);
	});
	connect(exportButton, &QPushButton::clicked, this, [this]() {
		SoundCatalog* cat = catalog();
		if (!cat) {
			return;
		}
		const std::string code = cat->GameCode();
		const QString name = QString::fromStdString(code.empty() ? std::string("sounds") : code) + QString::fromUtf8(".txt");
		const QString path = QFileDialog::getSaveFileName(this, QString::fromUtf8("소리 분류 내보내기"), name,
		                                                  QString::fromUtf8("소리 분류 (*.txt)"));
		if (path.isEmpty()) {
			return;
		}
		if (!cat->ExportTo(path.toStdString())) {
			QMessageBox::warning(this, windowTitle(), QString::fromUtf8("파일을 쓸 수 없습니다."));
		}
	});
	connect(clearButton, &QPushButton::clicked, this, [this]() {
		if (SoundCatalog* cat = catalog()) {
			cat->ClearSeen();
		}
		refresh(true);
	});

	m_timer = new QTimer(this);
	connect(m_timer, &QTimer::timeout, this, [this]() {
		refresh(false);
		updateMode();
		updatePlayButton();
		if (m_preview.isPlaying()) {
			const bool paused = m_preview.isPaused();
			const double sec = m_playElapsed + (paused ? 0 : (QDateTime::currentMSecsSinceEpoch() - m_playStartMs) / 1000.0);
			QString t = (paused ? QString::fromUtf8("일시정지: %1 — %2초") : QString::fromUtf8("재생 중: %1 — %2초"))
			                .arg(m_playingName)
			                .arg(sec, 0, 'f', 1);
			if (m_playLength > 0) {
				t += QString::fromUtf8(" / 약 %1초").arg(m_playLength, 0, 'f', 1);
			}
			m_playing->setText(t);
		} else {
			m_playing->setText(m_playingName.isEmpty() ? QString() : QString::fromUtf8("재생 끝: %1").arg(m_playingName));
		}
	});
	m_timer->start(300);

	catalog();
	refresh(true);
	updateMode();
}

void FfSoundCatalogDialog::updatePlayButton() {
	const QString text = m_preview.isPlaying() && !m_preview.isPaused() ? QString::fromUtf8("일시정지") : QString::fromUtf8("재생");
	if (m_playButton->text() != text) {
		m_playButton->setText(text);
	}
}

QString FfSoundCatalogDialog::selectedKey() const {
	const int r = m_table->currentRow();
	if (r < 0) {
		return {};
	}
	QTableWidgetItem* it = m_table->item(r, Col_Key);
	return it ? it->text() : QString();
}

void FfSoundCatalogDialog::applyKind(int kind) {
	const QString key = selectedKey();
	SoundCatalog* cat = catalog();
	if (key.isEmpty() || !cat) {
		return;
	}
	cat->SetUserKind(key.toStdString(), kind);
	refresh(true);
}

void FfSoundCatalogDialog::refresh(bool force) {
	if (m_editing) {
		return;
	}
	SoundCatalog* cat = catalog();
	const unsigned v = cat ? cat->SeenVersion() : 0;
	if (!force && v == m_shownVersion) {
		return;
	}
	m_shownVersion = v;
	const QString keep = selectedKey();
	const std::vector<SoundCatalog::Seen> list = cat ? cat->SeenList() : std::vector<SoundCatalog::Seen>();
	m_table->setSortingEnabled(false);
	m_table->setRowCount((int) list.size());
	for (int r = 0; r < (int) list.size(); r++) {
		const SoundCatalog::Seen& s = list[r];
		auto item = [&](int col) {
			QTableWidgetItem* it = m_table->item(r, col);
			if (!it) {
				it = new QTableWidgetItem();
				m_table->setItem(r, col, it);
			}
			return it;
		};
		item(Col_Order)->setData(Qt::DisplayRole, (int) s.order + 1);
		item(Col_Type)->setText(typeName(s.type));
		// 사용자가 붙인 이름이 있으면 그것을, 원래 이름은 풍선 도움말로
		item(Col_Name)->setText(QString::fromStdString(s.label.empty() ? s.name : s.label));
		item(Col_Name)->setToolTip(s.label.empty() ? QString::fromUtf8("두 번 클릭하면 이름을 바꿀 수 있습니다")
		                                           : QString::fromUtf8("원래 이름: %1").arg(QString::fromStdString(s.name)));
		item(Col_Auto)->setText(QString::fromUtf8(SoundCatalog::KindName(s.auto_kind)));
		item(Col_User)->setText(s.user_kind == SoundCatalog::Kind_Auto ? QString::fromUtf8("-") : QString::fromUtf8(SoundCatalog::KindName(s.user_kind)));
		item(Col_Volume)->setText(QString::fromUtf8("%1 %").arg(s.volume));
		item(Col_Count)->setData(Qt::DisplayRole, s.count);
		item(Col_Key)->setText(QString::fromStdString(s.key));
	}
	m_table->setSortingEnabled(true);
	if (!keep.isEmpty()) {
		for (int r = 0; r < m_table->rowCount(); r++) {
			if (m_table->item(r, Col_Key) && m_table->item(r, Col_Key)->text() == keep) {
				m_table->selectRow(r);
			}
		}
	}
}

void FfSoundCatalogDialog::updateMode() {
	SoundCatalog* cat = catalog();
	BgmReplacer* bgm = m_hooks.bgm ? m_hooks.bgm() : nullptr;
	QString game;
	if (!cat || cat->GameCode().empty() || !bgm || (!bgm->isGba() && !bgm->gbDriver())) {
		game = QString::fromUtf8("(게임 없음, 또는 소리를 알아볼 수 없는 게임)");
	} else {
		game = QString::fromStdString(cat->GameTitle() + " [" + cat->GameCode() + "]");
	}
	QString text;
	if (isActiveWindow()) {
		text = game + QString::fromUtf8(" — 일시정지 중. 에뮬레이터 창을 누르면 게임이 진행되며 나오는 소리를 모읍니다.\n분류는 바로 저장됩니다: ") +
		       QString::fromStdString(cat ? cat->StoragePath() : std::string());
	} else {
		text = game + QString::fromUtf8(" — 수집 중. 이 창을 누르면 게임이 일시정지됩니다.");
	}
	if (m_status->text() != text) {
		m_status->setText(text);
	}
}

void FfSoundCatalogDialog::pauseGame() {
	if (m_pausedByUs || !m_hooks.isRunning || !m_hooks.isRunning()) {
		return;
	}
	m_hooks.pause();
	m_pausedByUs = true;
}

void FfSoundCatalogDialog::resumeGame() {
	if (!m_pausedByUs) {
		return;
	}
	m_pausedByUs = false;
	if (m_hooks.resume) {
		m_hooks.resume();
	}
}

void FfSoundCatalogDialog::changeEvent(QEvent* event) {
	QDialog::changeEvent(event);
	if (event->type() != QEvent::ActivationChange || m_closing || !isVisible()) {
		return;
	}
	if (isActiveWindow()) {
		pauseGame();
	} else if (m_pausedByUs) {
		m_preview.stop();
		resumeGame();
	}
	updateMode();
}

void FfSoundCatalogDialog::hideEvent(QHideEvent* event) {
	m_closing = true;
	m_preview.stop();
	// 지금 게임의 것으로 다시 구한다 (게임을 닫았으면 예전 포인터는 쓰면 안 된다)
	if (SoundCatalog* cat = catalog()) {
		cat->SetCollecting(false);
	}
	resumeGame();
	QDialog::hideEvent(event);
	deleteLater();
}
