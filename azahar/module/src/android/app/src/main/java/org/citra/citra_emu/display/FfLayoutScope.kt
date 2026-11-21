// ff-bgm1x: 버튼·화면 배치의 "기본" 과 "이 게임" 나누기 (레무로이드와 같은 규칙)
//  - 방향(세로/가로)마다 이 게임 배치가 있으면 그것, 없으면 기본 배치를 쓴다.
//  - 이 게임 배치는 같은 설정 저장소에 "g:<게임>:<원래 키>" 로 둔다. 배치 키가 아닌 설정은 그대로.
//  - 처음 이 게임으로 저장할 때 그 방향의 기본 배치를 통째로 복사해 두므로, 그 뒤 기본을 바꿔도 이 게임 배치는 그대로.
package org.citra.citra_emu.display

import android.content.SharedPreferences
import android.content.res.Configuration
import androidx.preference.PreferenceManager
import org.citra.citra_emu.CitraApplication
import org.citra.citra_emu.NativeLibrary

object FfLayoutScope {
    /** 지금 켠 게임 (없으면 null = 모든 배치가 기본) */
    @Volatile
    var gameKey: String? = null
        private set

    /** 편집기가 저장하는 동안만: true = 이 게임, false = 기본 (null = 지금 쓰는 쪽) */
    @Volatile
    var forceGame: Boolean? = null

    private const val MARK = "ffGameLayout-"
    private const val SCREEN = "ffScreen"

    private val base: SharedPreferences
        get() = PreferenceManager.getDefaultSharedPreferences(CitraApplication.appContext)

    val prefs: SharedPreferences by lazy { ScopedPrefs() }

    fun isPortraitNow(): Boolean =
        CitraApplication.appContext.resources.configuration.orientation == Configuration.ORIENTATION_PORTRAIT

    private fun suffix(portrait: Boolean) = if (portrait) "-Portrait" else "-Landscape"
    private fun marker(game: String, portrait: Boolean) = MARK + game + suffix(portrait)
    private fun gameKeyOf(game: String, key: String) = "g:$game:$key"

    /** 배치 키인가 (버튼 자리·크기·표시, 기능 버튼, 그룹, 스킨, 게임 화면 자리) */
    fun isLayoutKey(k: String): Boolean =
        k.endsWith("-X") || k.endsWith("-Y") ||
            k.startsWith("buttonToggle") || k.startsWith("controlScale") ||
            k.startsWith("ffCustomButtons") || k.startsWith("ffLayoutGroups") ||
            k.startsWith("ffSkinDir") || k.startsWith("ffSkinBg") || k.startsWith("ffSkinHoles") ||
            k.startsWith(SCREEN)

    /** 그 키가 어느 방향 것인가 (null = 두 방향 공용) */
    private fun keyPortrait(k: String): Boolean? = when {
        k.contains("-Portrait") -> true
        k.endsWith("-Landscape") -> false
        k.endsWith("-X") || k.endsWith("-Y") -> false
        k == "ffLayoutGroups" || k == "ffSkinBg" || k == "ffSkinHoles" -> false
        else -> null
    }

    fun hasGameLayout(portrait: Boolean): Boolean {
        val g = gameKey ?: return false
        return base.getBoolean(marker(g, portrait), false)
    }

    private fun useGame(k: String, write: Boolean): Boolean {
        val g = gameKey ?: return false
        if (!isLayoutKey(k)) return false
        val p = keyPortrait(k) ?: isPortraitNow()
        if (write) forceGame?.let { return it }
        return base.getBoolean(marker(g, p), false)
    }

    // 이 게임 배치는 만들 때 기본 배치를 통째로 복사하므로, 없는 키는 정말 없는 것 (기본으로 되돌아가 읽지 않는다)
    private fun readKey(k: String): String {
        val g = gameKey ?: return k
        return if (useGame(k, false)) gameKeyOf(g, k) else k
    }

    /** [game] 쪽의 실제 저장 키 (편집기의 미리 보기 되돌리기용) */
    fun rawKey(k: String, game: Boolean): String {
        val g = gameKey
        return if (game && g != null) gameKeyOf(g, k) else k
    }

    /** 실제 저장 키들의 지금 값 (없으면 null) */
    fun snapshot(rawKeys: List<String>): Map<String, Any?> = rawKeys.associateWith { base.all[it] }

    /** [snapshot] 으로 받아 둔 값으로 되돌린다 */
    fun restore(values: Map<String, Any?>) {
        val e = base.edit()
        values.forEach { (k, v) ->
            when (v) {
                null -> e.remove(k)
                is Boolean -> e.putBoolean(k, v)
                is Int -> e.putInt(k, v)
                is Float -> e.putFloat(k, v)
                is Long -> e.putLong(k, v)
                is String -> e.putString(k, v)
            }
        }
        e.commit()
    }

    private fun writeKey(k: String): String {
        val g = gameKey ?: return k
        return if (useGame(k, true)) gameKeyOf(g, k) else k
    }

    /** 게임을 켤 때·끌 때. 켤 때 이 게임 화면 배치를 네이티브에 넘긴다 */
    fun setGame(key: String?) {
        gameKey = key
        forceGame = null
        pushScreenOverride()
    }

    /** 이 게임 배치를 (아직 없으면) 그 방향의 기본 배치를 복사해 만든다 */
    fun ensureGameLayout(portrait: Boolean) {
        val g = gameKey ?: return
        val m = marker(g, portrait)
        if (base.getBoolean(m, false)) return
        val e = base.edit()
        base.all.forEach { (k, v) ->
            if (k.startsWith("g:") || !isLayoutKey(k)) return@forEach
            val p = keyPortrait(k)
            if (p != null && p != portrait) return@forEach
            val gk = gameKeyOf(g, k)
            // 공용 키는 다른 방향 게임 배치가 이미 만들었으면 그대로 둔다
            if (p == null && base.contains(gk)) return@forEach
            when (v) {
                is Boolean -> e.putBoolean(gk, v)
                is Int -> e.putInt(gk, v)
                is Float -> e.putFloat(gk, v)
                is Long -> e.putLong(gk, v)
                is String -> e.putString(gk, v)
            }
        }
        e.putBoolean(m, true).commit()
    }

    /** 이 게임 배치를 지워 기본 배치로 돌아간다 (그 방향만) */
    fun clearGameLayout(portrait: Boolean) {
        val g = gameKey ?: return
        val other = base.getBoolean(marker(g, !portrait), false)
        val prefix = "g:$g:"
        val e = base.edit()
        base.all.keys.filter { it.startsWith(prefix) }.forEach { gk ->
            val p = keyPortrait(gk.removePrefix(prefix))
            if (p == portrait || (p == null && !other)) e.remove(gk)
        }
        e.remove(marker(g, portrait)).commit()
        pushScreenOverride()
    }

    // 게임 화면 자리 ----------------------------------------------------------

    /** 이 게임 화면 자리 (위 4 + 아래 4). 이 게임 배치가 아니거나 저장한 적 없으면 null */
    fun gameScreen(portrait: Boolean): IntArray? {
        val g = gameKey ?: return null
        if (!base.getBoolean(marker(g, portrait), false)) return null
        val s = base.getString(gameKeyOf(g, SCREEN + suffix(portrait)), null) ?: return null
        val v = s.split(',').mapNotNull { it.toIntOrNull() }
        return if (v.size == 8) v.toIntArray() else null
    }

    fun saveGameScreen(portrait: Boolean, top: IntArray, bottom: IntArray) {
        val g = gameKey ?: return
        base.edit().putString(gameKeyOf(g, SCREEN + suffix(portrait)), (top + bottom).joinToString(",")).commit()
        pushScreenOverride()
    }

    /** 네이티브에 이 게임 화면 자리를 넘긴다 (없으면 끄고 설정 파일 값으로) */
    fun pushScreenOverride() {
        var turnedOff = false
        for (p in listOf(false, true)) {
            val v = gameScreen(p)
            NativeLibrary.ffSetLayoutOverride(p, v != null, v)
            if (v == null) turnedOff = true
        }
        // 게임이 돌고 있을 때만 바로 다시 그린다 (켜기 전이면 게임 시작 때 네이티브가 넘긴 값을 쓴다)
        if (NativeLibrary.isRunning()) {
            if (turnedOff) NativeLibrary.reloadSettings()
            NativeLibrary.updateFramebuffer(NativeLibrary.isPortraitMode())
        }
    }

    // 배치 키만 게임 키로 바꿔 읽고 쓰는 설정 저장소 ------------------------------------

    private class ScopedPrefs : SharedPreferences {
        override fun getAll(): MutableMap<String, *> = base.all
        override fun getString(key: String, defValue: String?): String? = base.getString(readKey(key), defValue)
        override fun getStringSet(key: String, defValues: MutableSet<String>?): MutableSet<String>? =
            base.getStringSet(readKey(key), defValues)
        override fun getInt(key: String, defValue: Int): Int = base.getInt(readKey(key), defValue)
        override fun getLong(key: String, defValue: Long): Long = base.getLong(readKey(key), defValue)
        override fun getFloat(key: String, defValue: Float): Float = base.getFloat(readKey(key), defValue)
        override fun getBoolean(key: String, defValue: Boolean): Boolean = base.getBoolean(readKey(key), defValue)
        override fun contains(key: String): Boolean = base.contains(readKey(key))
        override fun edit(): SharedPreferences.Editor = ScopedEditor(base.edit())
        override fun registerOnSharedPreferenceChangeListener(l: SharedPreferences.OnSharedPreferenceChangeListener) =
            base.registerOnSharedPreferenceChangeListener(l)
        override fun unregisterOnSharedPreferenceChangeListener(l: SharedPreferences.OnSharedPreferenceChangeListener) =
            base.unregisterOnSharedPreferenceChangeListener(l)
    }

    private class ScopedEditor(private val e: SharedPreferences.Editor) : SharedPreferences.Editor {
        override fun putString(key: String, value: String?) = apply { e.putString(writeKey(key), value) }
        override fun putStringSet(key: String, values: MutableSet<String>?) = apply { e.putStringSet(writeKey(key), values) }
        override fun putInt(key: String, value: Int) = apply { e.putInt(writeKey(key), value) }
        override fun putLong(key: String, value: Long) = apply { e.putLong(writeKey(key), value) }
        override fun putFloat(key: String, value: Float) = apply { e.putFloat(writeKey(key), value) }
        override fun putBoolean(key: String, value: Boolean) = apply { e.putBoolean(writeKey(key), value) }
        override fun remove(key: String) = apply { e.remove(writeKey(key)) }
        override fun clear() = apply { e.clear() }
        override fun commit(): Boolean = e.commit()
        override fun apply() = e.apply()
    }
}
