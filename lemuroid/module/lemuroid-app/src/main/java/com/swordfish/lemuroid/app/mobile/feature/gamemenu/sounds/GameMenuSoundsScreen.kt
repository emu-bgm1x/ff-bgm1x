package com.swordfish.lemuroid.app.mobile.feature.gamemenu.sounds

import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioTrack
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Divider
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Slider
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import com.swordfish.lemuroid.R
import com.swordfish.libretrodroid.LibretroDroid
import kotlinx.coroutines.delay
import java.io.File
import kotlin.concurrent.thread

// ff-bgm1x: 소리 분류 화면 (1x Lab 앱, 게임 메뉴 안). PC 판 분류 창과 같은 일을 한다.
// 게임 메뉴가 열려 있는 동안 게임은 멈춰 있다. "소리 수집" 을 켜 두면 메뉴를 닫고 게임을 하는 동안 소리가 목록에 쌓인다.
// 코어와는 LibretroDroid.catalogCommand(문자열 명령, common/catalog_command.h) 로 이야기한다.

private data class SoundRow(
    val key: String,
    val type: Int,
    val name: String,
    val label: String,
    val autoKind: Int,
    val userKind: Int,
    val volume: Int,
    val count: Int,
    val order: Int,
)

private object Catalog {
    fun cmd(c: String): String = runCatching { LibretroDroid.catalogCommand(c) }.getOrDefault("error\t")

    fun state(): Map<String, List<String>> =
        cmd("state").lines().filter { it.isNotEmpty() }.associate {
            val parts = it.split('\t')
            parts[0] to parts.drop(1)
        }

    fun list(): List<SoundRow> =
        cmd("list").lines().mapNotNull { line ->
            val p = line.split('\t')
            if (p.size < 9) return@mapNotNull null
            SoundRow(
                p[0], p[1].toIntOrNull() ?: 0, p[2], p[3], p[4].toIntOrNull() ?: 0, p[5].toIntOrNull() ?: -1,
                p[6].toIntOrNull() ?: 100, p[7].toIntOrNull() ?: 0, p[8].toIntOrNull() ?: 0,
            )
        }.sortedBy { it.order }
}

// 재생 소리: 코어가 만든 소리를 AudioTrack 으로 낸다 (화면이 열려 있는 동안)
private class PreviewOutput {
    @Volatile private var running = false
    private var worker: Thread? = null

    fun start() {
        if (running) return
        running = true
        worker =
            thread(name = "ff-bgm1x-preview") {
                val rate = 48000
                val frames = 1024
                val minBytes = AudioTrack.getMinBufferSize(rate, AudioFormat.CHANNEL_OUT_STEREO, AudioFormat.ENCODING_PCM_16BIT)
                val track =
                    AudioTrack.Builder()
                        .setAudioAttributes(
                            AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_GAME)
                                .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build(),
                        )
                        .setAudioFormat(
                            AudioFormat.Builder().setSampleRate(rate).setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                                .setChannelMask(AudioFormat.CHANNEL_OUT_STEREO).build(),
                        )
                        .setBufferSizeInBytes(maxOf(minBytes, frames * 4 * 2))
                        .setTransferMode(AudioTrack.MODE_STREAM)
                        .build()
                track.play()
                val buffer = ShortArray(frames * 2)
                while (running) {
                    java.util.Arrays.fill(buffer, 0)
                    runCatching { LibretroDroid.previewRender(buffer, frames, rate) }
                    track.write(buffer, 0, buffer.size)
                }
                track.stop()
                track.release()
            }
    }

    fun stop() {
        running = false
        worker?.join(500)
        worker = null
    }
}

@Composable
private fun kindName(kind: Int): String =
    stringResource(
        when (kind) {
            -2 -> R.string.sounds_kind_unknown
            -1 -> R.string.sounds_kind_auto
            0 -> R.string.sounds_kind_none
            1 -> R.string.sounds_kind_bgm
            2 -> R.string.sounds_kind_jingle
            3 -> R.string.sounds_kind_cutscene
            4 -> R.string.sounds_kind_sfx
            else -> R.string.sounds_kind_auto
        },
    )

private val KINDS = listOf(-1, 1, 2, 3, 4, 0)

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun GameMenuSoundsScreen() {
    val context = LocalContext.current
    var rows by remember { mutableStateOf<List<SoundRow>>(emptyList()) }
    var collecting by remember { mutableStateOf(false) }
    var usable by remember { mutableStateOf(true) }
    var gameCode by remember { mutableStateOf("") }
    var expanded by remember { mutableStateOf<String?>(null) }
    var playing by remember { mutableStateOf(false) }
    var paused by remember { mutableStateOf(false) }
    var playingKey by remember { mutableStateOf<String?>(null) }
    var renaming by remember { mutableStateOf<SoundRow?>(null) }
    var refresh by remember { mutableStateOf(0) }
    val output = remember { PreviewOutput() }

    fun toast(message: String) = Toast.makeText(context, message, Toast.LENGTH_LONG).show()

    fun run(c: String) {
        val r = Catalog.cmd(c)
        if (r.startsWith("error\t")) toast(r.removePrefix("error\t"))
        refresh++
    }

    DisposableEffect(Unit) {
        Catalog.cmd("dir " + File(context.filesDir, "ff-bgm1x-sounds").absolutePath)
        output.start()
        onDispose {
            Catalog.cmd("stop")
            output.stop()
        }
    }

    // 목록과 재생 상태를 주기적으로 다시 읽는다 (수집 중에는 늘어난다)
    LaunchedEffect(refresh) {
        var shownVersion = ""
        while (true) {
            val st = Catalog.state()
            collecting = st["collect"]?.firstOrNull() == "1"
            usable = st["usable"]?.firstOrNull() == "1"
            gameCode = st["game"]?.firstOrNull().orEmpty()
            val v = st["version"]?.firstOrNull().orEmpty()
            if (v != shownVersion) {
                shownVersion = v
                rows = Catalog.list()
            }
            val ps =
                Catalog.cmd("pstate").lines().filter { it.isNotEmpty() }.associate {
                    val parts = it.split('\t')
                    parts[0] to parts.getOrElse(1) { "" }
                }
            playing = ps["playing"] == "1"
            paused = ps["paused"] == "1"
            delay(400)
        }
    }

    val exportLauncher =
        rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("text/plain")) { uri ->
            if (uri == null) return@rememberLauncherForActivityResult
            runCatching {
                context.contentResolver.openOutputStream(uri)?.use { it.write(Catalog.cmd("export").toByteArray(Charsets.UTF_8)) }
            }.onFailure { toast(it.message ?: "") }
        }
    val importLauncher =
        rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
            if (uri == null) return@rememberLauncherForActivityResult
            val text = runCatching { context.contentResolver.openInputStream(uri)?.use { it.readBytes().toString(Charsets.UTF_8) } }.getOrNull()
            if (text != null) run("import\n$text")
        }

    Column(modifier = Modifier.fillMaxSize()) {
        if (!usable) {
            Text(stringResource(R.string.sounds_unsupported), modifier = Modifier.padding(16.dp))
        }
        Row(
            modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Column(modifier = Modifier.weight(1f)) {
                Text(stringResource(R.string.sounds_collect), style = MaterialTheme.typography.titleMedium)
                Text(stringResource(R.string.sounds_collect_hint), style = MaterialTheme.typography.bodySmall)
            }
            Switch(checked = collecting, onCheckedChange = { run("collect " + if (it) 1 else 0) })
        }
        FlowRow(
            modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            OutlinedButton(onClick = { importLauncher.launch(arrayOf("*/*")) }) { Text(stringResource(R.string.sounds_import)) }
            OutlinedButton(onClick = { exportLauncher.launch((gameCode.ifEmpty { "sounds" }) + ".txt") }) {
                Text(stringResource(R.string.sounds_export))
            }
            OutlinedButton(onClick = { run("clear") }) { Text(stringResource(R.string.sounds_clear)) }
        }
        Divider(modifier = Modifier.padding(top = 8.dp))
        if (rows.isEmpty()) {
            Text(stringResource(R.string.sounds_empty), modifier = Modifier.padding(16.dp))
        }
        LazyColumn(modifier = Modifier.fillMaxSize()) {
            items(rows, key = { it.key }) { row ->
                Column(
                    modifier =
                        Modifier.fillMaxWidth()
                            .clickable { expanded = if (expanded == row.key) null else row.key }
                            .padding(horizontal = 16.dp, vertical = 10.dp),
                ) {
                    Text(row.label.ifEmpty { row.name }, style = MaterialTheme.typography.bodyLarge)
                    val user = if (row.userKind == -1) "-" else kindName(row.userKind)
                    Text(
                        stringResource(R.string.sounds_auto) + ": " + kindName(row.autoKind) + "  ·  " +
                            stringResource(R.string.sounds_kind) + ": " + user + "  ·  " +
                            stringResource(R.string.sounds_volume) + " " + row.volume + "%  ·  " +
                            stringResource(R.string.sounds_count, row.count),
                        style = MaterialTheme.typography.bodySmall,
                    )
                    if (expanded == row.key) {
                        SoundControls(
                            row = row,
                            isPlaying = playing && !paused && playingKey == row.key,
                            onPlay = {
                                when {
                                    playing && !paused && playingKey == row.key -> run("pause 1")
                                    playing && paused && playingKey == row.key -> run("pause 0")
                                    else -> {
                                        playingKey = row.key
                                        run("play " + row.key)
                                    }
                                }
                            },
                            onStop = { run("stop") },
                            onKind = { run("kind ${row.key} $it") },
                            onVolume = { run("vol ${row.key} $it") },
                            onRename = { renaming = row },
                            onReset = { run("reset ${row.key}") },
                        )
                    }
                }
                Divider()
            }
        }
    }

    renaming?.let { row ->
        var text by remember(row.key) { mutableStateOf(row.label.ifEmpty { row.name }) }
        AlertDialog(
            onDismissRequest = { renaming = null },
            title = { Text(stringResource(R.string.sounds_rename)) },
            text = {
                OutlinedTextField(
                    value = text,
                    onValueChange = { text = it.replace('\n', ' ') },
                    placeholder = { Text(stringResource(R.string.sounds_name_hint)) },
                    singleLine = true,
                )
            },
            confirmButton = {
                TextButton(onClick = {
                    val t = text.trim()
                    // 원래 이름을 그대로 두면 이름을 붙이지 않는다
                    if (t != row.name || row.label.isNotEmpty()) run("label ${row.key} $t")
                    renaming = null
                }) { Text(stringResource(android.R.string.ok)) }
            },
            dismissButton = { TextButton(onClick = { renaming = null }) { Text(stringResource(android.R.string.cancel)) } },
        )
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun SoundControls(
    row: SoundRow,
    isPlaying: Boolean,
    onPlay: () -> Unit,
    onStop: () -> Unit,
    onKind: (Int) -> Unit,
    onVolume: (Int) -> Unit,
    onRename: () -> Unit,
    onReset: () -> Unit,
) {
    var kindMenu by remember { mutableStateOf(false) }
    var volume by remember(row.key, row.volume) { mutableFloatStateOf(row.volume.toFloat()) }
    Column(modifier = Modifier.padding(top = 8.dp)) {
        FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            OutlinedButton(onClick = onPlay) {
                Text(stringResource(if (isPlaying) R.string.sounds_pause else R.string.sounds_play))
            }
            OutlinedButton(onClick = onStop) { Text(stringResource(R.string.sounds_stop)) }
            Box {
                OutlinedButton(onClick = { kindMenu = true }) {
                    Text(stringResource(R.string.sounds_kind) + ": " + kindName(row.userKind))
                }
                DropdownMenu(expanded = kindMenu, onDismissRequest = { kindMenu = false }) {
                    KINDS.forEach { k ->
                        DropdownMenuItem(
                            text = { Text(kindName(k)) },
                            onClick = {
                                kindMenu = false
                                onKind(k)
                            },
                        )
                    }
                }
            }
            OutlinedButton(onClick = onRename) { Text(stringResource(R.string.sounds_rename)) }
            OutlinedButton(onClick = onReset) { Text(stringResource(R.string.sounds_reset)) }
        }
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(stringResource(R.string.sounds_volume) + " " + volume.toInt() + "%", modifier = Modifier.padding(end = 8.dp))
            Slider(
                value = volume,
                onValueChange = { volume = it },
                onValueChangeFinished = { onVolume(volume.toInt()) },
                valueRange = 0f..200f,
                steps = 39,
                modifier = Modifier.weight(1f),
            )
        }
    }
}
