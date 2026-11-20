/* ff-bgm1x: 소리 분류 창 (melonDS·DeSmuME·Azahar 판과 같은 사양).
 *
 * 이 창이 앞에 있으면 게임을 일시정지하고, 에뮬레이터 창을 누르면 게임을 다시 돌리며 나온 소리를 목록에 모은다(수집 모드).
 * 목록의 소리마다 재생, 분류 지정(BGM / 팡파레·대사 / 연출 / 효과음 / 아무것도 아님), 볼륨, 기본값(자동)으로 되돌리기.
 * 이름·지정·볼륨 칸을 두 번 클릭하면 그 자리에서 바꾸고, 그 밖의 칸을 두 번 클릭하면 재생한다
 * (GBA 곡은 롬에 이름이 없어 사용자가 이름을 붙일 수 있다).
 * 분류는 게임별 파일에 바로 저장하고, 내보내기·가져오기로 다른 사람과 주고받는다.
 * 창이 닫혀 있으면 수집하지 않는다 (성능에 영향 없음). GB/GBC 는 1배속 재생기가 지원하는 사운드 엔진의 게임만. */
#pragma once

#include <functional>
#include <QDialog>

#include "FfSoundPreview.h"

class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTimer;
class SoundCatalog;

namespace QGBA {

class BgmReplacer;

class FfSoundCatalogDialog : public QDialog {
Q_OBJECT

public:
	struct Hooks {
		std::function<BgmReplacer*()> bgm;  // 지금 게임의 BGM 처리 (게임이 없으면 nullptr)
		std::function<bool()> isRunning;   // 게임이 돌고 있는지 (일시정지가 아님)
		std::function<void()> pause;
		std::function<void()> resume;
	};
	static void open(QWidget* parent, Hooks hooks);
	static void closeIfOpen();
	~FfSoundCatalogDialog() override;

protected:
	void changeEvent(QEvent* event) override;
	void hideEvent(QHideEvent* event) override;

private:
	FfSoundCatalogDialog(QWidget* parent, Hooks hooks);
	SoundCatalog* catalog();
	void refresh(bool force);
	void updateMode();
	QString selectedKey() const;
	void applyKind(int kind);
	void updatePlayButton();
	void pauseGame();
	void resumeGame();

	static FfSoundCatalogDialog* s_current;
	Hooks m_hooks;
	FfSoundPreview m_preview;
	QTableWidget* m_table;
	QComboBox* m_kindBox;
	QSpinBox* m_volumeBox;
	QLabel* m_status;
	QLabel* m_playing;
	QPushButton* m_playButton;
	QTimer* m_timer;
	SoundCatalog* m_shownCatalog = nullptr;
	unsigned m_shownVersion = ~0u;
	bool m_pausedByUs = false;
	bool m_updatingUi = false;
	bool m_editing = false;
	bool m_closing = false;
	QString m_playingName;
	QString m_playingKey;
	double m_playElapsed = 0;
	qint64 m_playStartMs = 0;
	double m_playLength = 0;
};

}
