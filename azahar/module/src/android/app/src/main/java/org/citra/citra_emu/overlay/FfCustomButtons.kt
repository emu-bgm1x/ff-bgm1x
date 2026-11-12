// ff-bgm1x: 사용자가 배치 편집기에서 만드는 화면 버튼 (에뮬레이터 기능 버튼).
// 목록은 앱 설정 "ffCustomButtons" 에 "id:기능,id:기능" 으로 저장한다.
// 자리·크기는 다른 화면 버튼과 같은 키("<id><방향>-X/Y", "controlScale-<id>")를 쓴다.
package org.citra.citra_emu.overlay

import android.content.SharedPreferences
import org.citra.citra_emu.R

object FfCustomButtons {
    const val FIRST_ID = 900
    const val LAST_ID = 999
    private const val PREF_KEY = "ffCustomButtons"

    // 버튼이 하는 일. code 는 저장용 번호 (바꾸지 말 것)
    enum class Function(val code: Int, val labelRes: Int) {
        NONE(0, R.string.ff_func_none),
        MENU(1, R.string.ff_func_menu),
        SWAP_SCREEN(2, R.string.ff_func_swap_screen),
        CYCLE_LAYOUT(3, R.string.ff_func_cycle_layout),
        PAUSE_OR_RESUME(4, R.string.ff_func_pause),
        QUICKSAVE(5, R.string.ff_func_quicksave),
        QUICKLOAD(6, R.string.ff_func_quickload),
        TURBO(7, R.string.ff_func_turbo),
        CLOSE_GAME(8, R.string.ff_func_close_game),
        // ff-bgm1x 1배속 옵션 켜기/끄기
        BGM_1X(9, R.string.ff_func_bgm_1x),
        VOICES_1X(10, R.string.ff_func_voices_1x),
        FF_CUTSCENES(11, R.string.ff_func_ff_cutscenes);

        companion object {
            fun of(code: Int) = entries.firstOrNull { it.code == code } ?: NONE
        }
    }

    fun isCustom(id: Int) = id in FIRST_ID..LAST_ID

    fun load(prefs: SharedPreferences): LinkedHashMap<Int, Function> {
        val map = LinkedHashMap<Int, Function>()
        prefs.getString(PREF_KEY, "")!!.split(',').forEach { entry ->
            val parts = entry.split(':')
            if (parts.size != 2) return@forEach
            val id = parts[0].toIntOrNull() ?: return@forEach
            val code = parts[1].toIntOrNull() ?: return@forEach
            if (isCustom(id)) map[id] = Function.of(code)
        }
        return map
    }

    fun save(prefs: SharedPreferences, map: Map<Int, Function>) {
        prefs.edit().putString(PREF_KEY, map.entries.joinToString(",") { "${it.key}:${it.value.code}" }).apply()
    }

    // 지금 쓰지 않는 가장 작은 번호
    fun nextId(used: Collection<Int>): Int? = (FIRST_ID..LAST_ID).firstOrNull { it !in used }
}
