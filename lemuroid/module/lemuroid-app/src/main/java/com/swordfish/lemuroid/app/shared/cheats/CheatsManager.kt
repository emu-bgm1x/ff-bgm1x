package com.swordfish.lemuroid.app.shared.cheats

import android.content.Context
import com.swordfish.lemuroid.lib.library.db.entity.Game
import kotlinx.serialization.Serializable
import kotlinx.serialization.builtins.ListSerializer
import kotlinx.serialization.json.Json
import java.io.File

// ff-bgm1x: 게임별 치트 목록 (앱 내부 저장소 cheats/<게임 파일 이름>.json)
@Serializable
data class Cheat(
    val name: String,
    val code: String,
    val enabled: Boolean,
)

class CheatsManager(private val appContext: Context) {
    private val json = Json { ignoreUnknownKeys = true }
    private val serializer = ListSerializer(Cheat.serializer())

    private fun file(game: Game): File {
        val safeName = game.fileName.replace(Regex("[^A-Za-z0-9._-]"), "_")
        return File(File(appContext.filesDir, "cheats"), "$safeName.json")
    }

    fun load(game: Game): List<Cheat> {
        val file = file(game)
        if (!file.exists()) return emptyList()
        return runCatching { json.decodeFromString(serializer, file.readText()) }.getOrDefault(emptyList())
    }

    fun save(
        game: Game,
        cheats: List<Cheat>,
    ) {
        val file = file(game)
        file.parentFile?.mkdirs()
        file.writeText(json.encodeToString(serializer, cheats))
    }

    companion object {
        // 코어에 넘길 코드: 켜진 치트만, 순서대로
        fun enabledCodes(cheats: List<Cheat>): List<String> = cheats.filter { it.enabled }.map { it.code }

        // RetroArch .cht 형식:
        //   cheats = 2
        //   cheat0_desc = "이름"
        //   cheat0_code = "XXXXXXXX+YYYYYYYY"
        //   cheat0_enable = false
        fun parseCht(text: String): List<Cheat> {
            val values = mutableMapOf<String, String>()
            text.lineSequence().forEach { line ->
                val index = line.indexOf('=')
                if (index <= 0) return@forEach
                val key = line.substring(0, index).trim()
                val value = line.substring(index + 1).trim().removeSurrounding("\"")
                values[key] = value
            }
            val count = values["cheats"]?.toIntOrNull() ?: return emptyList()
            return (0 until count).mapNotNull { i ->
                val code = values["cheat${i}_code"]?.takeIf { it.isNotBlank() } ?: return@mapNotNull null
                Cheat(
                    name = values["cheat${i}_desc"]?.takeIf { it.isNotBlank() } ?: "Cheat ${i + 1}",
                    code = code,
                    enabled = values["cheat${i}_enable"] == "true",
                )
            }
        }
    }
}
