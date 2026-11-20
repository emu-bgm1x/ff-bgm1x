package me.magnum.melonds.ui.soundcatalog

import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioTrack
import android.os.Bundle
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
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
import androidx.compose.material.Divider
import androidx.compose.material.DropdownMenu
import androidx.compose.material.DropdownMenuItem
import androidx.compose.material.Icon
import androidx.compose.material.IconButton
import androidx.compose.material.MaterialTheme
import androidx.compose.material.OutlinedButton
import androidx.compose.material.Scaffold
import androidx.compose.material.Switch
import androidx.compose.material.Text
import androidx.compose.material.TopAppBar
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.delay
import me.magnum.melonds.MelonEmulator
import me.magnum.melonds.R
import me.magnum.melonds.ui.theme.MelonTheme
import kotlin.concurrent.thread

// ff-bgm1x: 소리 분류 화면 (1x Lab 앱). PC 판 분류 창과 같은 일을 한다.
// 이 화면이 열려 있는 동안 게임은 멈춰 있다. "소리 수집" 을 켜 두면 화면을 닫고 게임을 하는 동안 소리가 목록에 쌓인다.
// 코어와는 MelonEmulator.catalogCommand(문자열 명령) 로 이야기한다.
class SoundCatalogActivity : AppCompatActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            MelonTheme {
                Scaffold(
                    topBar = {
                        TopAppBar(
                            title = { Text(stringResource(R.string.ff_sound_catalog)) },
                            navigationIcon = {
                                IconButton(onClick = { finish() }) {
                                    Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = null)
                                }
                            },
                        )
                    },
                ) { padding ->
                    Box(modifier = Modifier.padding(padding)) {
                        SoundCatalogScreen()
                    }
                }
            }
        }
    }
}

private data class SoundRow(
    val key: String,
    val type: Int,
    val name: String,
    val autoKind: Int,
    val userKind: Int,
    val volume: Int,
    val count: Int,
    val order: Int,
)

private object Catalog {
    fun cmd(c: String): String = runCatching { MelonEmulator.catalogCommand(c) }.getOrDefault("error\t")

    fun pairs(text: String): Map<String, String> =
        text.lines().filter { it.isNotEmpty() }.associate {
            val parts = it.split('\t')
            parts[0] to parts.getOrElse(1) { "" }
        }

    fun list(): List<SoundRow> =
        cmd("list").lines().mapNotNull { line ->
            val p = line.split('\t')
            if (p.size < 9) return@mapNotNull null
            SoundRow(
                p[0], p[1].toIntOrNull() ?: 0, p[2], p[4].toIntOrNull() ?: 0, p[5].toIntOrNull() ?: -1,
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
                    runCatching { MelonEmulator.previewRender(buffer, frames, rate) }
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

@Composable
private fun SoundCatalogScreen() {
    val context = LocalContext.current
    var rows by remember { mutableStateOf<List<SoundRow>>(emptyList()) }
    var collecting by remember { mutableStateOf(false) }
    var usable by remember { mutableStateOf(true) }
    var gameCode by remember { mutableStateOf("") }
    var expanded by remember { mutableStateOf<String?>(null) }
    var playing by remember { mutableStateOf(false) }
    var paused by remember { mutableStateOf(false) }
    var playingKey by remember { mutableStateOf<String?>(null) }
    var refresh by remember { mutableIntStateOf(0) }
    val output = remember { PreviewOutput() }

    fun toast(message: String) = Toast.makeText(context, message, Toast.LENGTH_LONG).show()

    fun run(c: String) {
        val r = Catalog.cmd(c)
        if (r.startsWith("error\t")) toast(r.removePrefix("error\t"))
        refresh++
    }

    DisposableEffect(Unit) {
        output.start()
        onDispose {
            Catalog.cmd("stop")
            output.stop()
        }
    }

    // 목록과 재생 상태를 주기적으로 다시 읽는다
    LaunchedEffect(refresh) {
        var shownVersion = ""
        while (true) {
            val st = Catalog.pairs(Catalog.cmd("state"))
            collecting = st["collect"] == "1"
            usable = st["usable"] == "1"
            gameCode = st["game"].orEmpty()
            val v = st["version"].orEmpty()
            if (v != shownVersion) {
                shownVersion = v
                rows = Catalog.list()
            }
            val ps = Catalog.pairs(Catalog.cmd("pstate"))
            playing = ps["playing"] == "1"
            paused = ps["paused"] == "1"
            delay(400)
        }
    }

    val exportLauncher =
        rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("text/plain")) { uri ->
            if (uri == null) return@rememberLauncherForActivityResult
            val text = Catalog.cmd("export")
            if (text.startsWith("error\t")) {
                toast(text.removePrefix("error\t"))
                return@rememberLauncherForActivityResult
            }
            runCatching {
                context.contentResolver.openOutputStream(uri)?.use { it.write(text.toByteArray(Charsets.UTF_8)) }
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
            return@Column
        }
        Row(
            modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Column(modifier = Modifier.weight(1f)) {
                Text(stringResource(R.string.sounds_collect), style = MaterialTheme.typography.subtitle1)
                Text(stringResource(R.string.sounds_collect_hint), style = MaterialTheme.typography.caption)
            }
            Switch(checked = collecting, onCheckedChange = { run("collect " + if (it) 1 else 0) })
        }
        ActionsRow(
            onImport = { importLauncher.launch(arrayOf("*/*")) },
            onExport = { exportLauncher.launch((gameCode.split('\t').firstOrNull().orEmpty().ifEmpty { "sounds" }) + ".txt") },
            onClear = { run("clear") },
        )
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
                    Text(row.name.ifEmpty { row.key }, style = MaterialTheme.typography.body1)
                    val user = if (row.userKind == -1) "-" else kindName(row.userKind)
                    Text(
                        stringResource(R.string.sounds_auto) + ": " + kindName(row.autoKind) + "  ·  " +
                            stringResource(R.string.sounds_kind) + ": " + user + "  ·  " +
                            stringResource(R.string.sounds_volume) + " " + row.volume + "%  ·  " +
                            stringResource(R.string.sounds_count, row.count) + "  ·  " + row.key,
                        style = MaterialTheme.typography.caption,
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
                            onReset = { run("reset ${row.key}") },
                        )
                    }
                }
                Divider()
            }
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun ActionsRow(onImport: () -> Unit, onExport: () -> Unit, onClear: () -> Unit) {
    FlowRow(
        modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp),
        horizontalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        OutlinedButton(onClick = onImport) { Text(stringResource(R.string.sounds_import)) }
        OutlinedButton(onClick = onExport) { Text(stringResource(R.string.sounds_export)) }
        OutlinedButton(onClick = onClear) { Text(stringResource(R.string.sounds_clear)) }
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
                        DropdownMenuItem(onClick = {
                            kindMenu = false
                            onKind(k)
                        }) { Text(kindName(k)) }
                    }
                }
            }
            OutlinedButton(onClick = onReset) { Text(stringResource(R.string.sounds_reset)) }
        }
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(stringResource(R.string.sounds_volume) + " " + volume.toInt() + "%", modifier = Modifier.padding(end = 8.dp))
            androidx.compose.material3.Slider(
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
