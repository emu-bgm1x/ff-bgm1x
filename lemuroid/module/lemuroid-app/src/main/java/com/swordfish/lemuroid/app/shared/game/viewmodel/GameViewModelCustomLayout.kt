package com.swordfish.lemuroid.app.shared.game.viewmodel

import android.content.SharedPreferences
import com.swordfish.lemuroid.lib.library.db.entity.Game
import com.swordfish.touchinput.radial.custom.CustomLayout
import com.swordfish.touchinput.radial.settings.TouchControllerID
import com.swordfish.touchinput.radial.settings.TouchControllerSettingsManager
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.serialization.json.Json

// ff-bgm1x: 버튼·게임 화면 자유 배치 저장.
// 기종 배치(TouchControllerID)와 가로/세로마다 "기본" 과 "이 게임" 두 가지를 둔다.
// 게임 배치가 있으면 그것, 없으면 기본, 둘 다 없으면 레무로이드 원래 배치(빈 CustomLayout).
@OptIn(ExperimentalCoroutinesApi::class)
class GameViewModelCustomLayout(
    private val sharedPreferences: SharedPreferences,
    private val game: Game,
) {
    data class Loaded(
        val layout: CustomLayout,
        val isGameSpecific: Boolean,
    )

    private val json = Json { ignoreUnknownKeys = true }
    private val version = MutableStateFlow(0)

    private fun key(
        controllerId: TouchControllerID,
        orientation: TouchControllerSettingsManager.Orientation,
        forGame: Boolean,
    ): String {
        val scope = if (forGame) "game_${game.fileName}" else "default"
        return "ff_bgm1x_custom_layout_${scope}_${controllerId}_${orientation.ordinal}"
    }

    private fun read(key: String): CustomLayout? {
        val text = sharedPreferences.getString(key, null) ?: return null
        return runCatching { json.decodeFromString(CustomLayout.serializer(), text) }.getOrNull()
    }

    fun load(
        controllerId: TouchControllerID,
        orientation: TouchControllerSettingsManager.Orientation,
    ): Loaded {
        read(key(controllerId, orientation, true))?.let { return Loaded(it, true) }
        return Loaded(read(key(controllerId, orientation, false)) ?: CustomLayout(), false)
    }

    fun observe(
        controllerIds: Flow<TouchControllerID>,
        orientations: Flow<TouchControllerSettingsManager.Orientation>,
    ): Flow<Loaded> = combine(controllerIds, orientations, version) { id, orientation, _ -> load(id, orientation) }

    fun save(
        controllerId: TouchControllerID,
        orientation: TouchControllerSettingsManager.Orientation,
        layout: CustomLayout,
        forGame: Boolean,
    ) {
        sharedPreferences.edit()
            .putString(key(controllerId, orientation, forGame), json.encodeToString(CustomLayout.serializer(), layout))
            .apply()
        version.value++
    }

    // forGame 이면 이 게임 배치만 지워 기본 배치로 돌아가고, 아니면 기본 배치를 지워 레무로이드 원래 배치로 돌아간다
    fun clear(
        controllerId: TouchControllerID,
        orientation: TouchControllerSettingsManager.Orientation,
        forGame: Boolean,
    ) {
        sharedPreferences.edit().remove(key(controllerId, orientation, forGame)).apply()
        version.value++
    }
}
