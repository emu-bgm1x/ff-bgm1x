// ff-bgm1x: 소리 분류 화면 (1x Lab 앱). PC 판 분류 창과 같은 일을 한다.
// 이 화면이 열려 있는 동안 게임은 멈춰 있다. "소리 수집" 을 켜 두면 화면을 닫고 게임을 하는 동안 소리가 목록에 쌓인다.
// 코어와는 NativeLibrary.catalogCommand(문자열 명령) 로 이야기한다.
package org.citra.citra_emu.activities

import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioTrack
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.google.android.material.appbar.MaterialToolbar
import com.google.android.material.button.MaterialButton
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.google.android.material.materialswitch.MaterialSwitch
import com.google.android.material.slider.Slider
import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.R
import kotlin.concurrent.thread

class FfSoundCatalogActivity : AppCompatActivity() {
    private data class Row(
        val key: String,
        val type: Int,
        val name: String,
        val autoKind: Int,
        val userKind: Int,
        val volume: Int,
        val count: Int,
        val order: Int,
    )

    private val kinds = listOf(-1, 1, 2, 3, 4, 0)
    private var rows: List<Row> = emptyList()
    private var shownVersion = ""
    private var gameCode = ""
    private var playing = false
    private var paused = false
    private var playingKey: String? = null

    private lateinit var collectSwitch: MaterialSwitch
    private lateinit var emptyText: TextView
    private lateinit var unsupportedText: TextView
    private lateinit var content: View
    private val adapter = RowAdapter()
    private val handler = Handler(Looper.getMainLooper())
    private var updatingSwitch = false

    @Volatile private var outputRunning = false
    private var outputThread: Thread? = null

    private val exportLauncher =
        registerForActivityResult(ActivityResultContracts.CreateDocument("text/plain")) { uri ->
            if (uri == null) return@registerForActivityResult
            val text = cmd("export")
            if (text.startsWith("error\t")) {
                toast(text.removePrefix("error\t"))
                return@registerForActivityResult
            }
            runCatching {
                contentResolver.openOutputStream(uri)?.use { it.write(text.toByteArray(Charsets.UTF_8)) }
            }.onFailure { toast(it.message ?: "") }
        }
    private val importLauncher =
        registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
            if (uri == null) return@registerForActivityResult
            val text = runCatching {
                contentResolver.openInputStream(uri)?.use { it.readBytes().toString(Charsets.UTF_8) }
            }.getOrNull()
            if (text != null) run("import\n$text")
        }

    private val poll = object : Runnable {
        override fun run() {
            refresh(false)
            handler.postDelayed(this, 400)
        }
    }

    private fun cmd(c: String): String =
        runCatching { NativeLibrary.catalogCommand(c) }.getOrDefault("error\t")

    private fun pairs(text: String): Map<String, String> =
        text.lines().filter { it.isNotEmpty() }.associate {
            val p = it.split('\t')
            p[0] to p.getOrElse(1) { "" }
        }

    private fun toast(message: String) = Toast.makeText(this, message, Toast.LENGTH_LONG).show()

    private fun run(c: String) {
        val r = cmd(c)
        if (r.startsWith("error\t")) toast(r.removePrefix("error\t"))
        refresh(true)
    }

    private fun dp(v: Int) = (v * resources.displayMetrics.density).toInt()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val root = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        val toolbar = MaterialToolbar(this).apply {
            title = getString(R.string.ff_sound_catalog)
            setNavigationIcon(androidx.appcompat.R.drawable.abc_ic_ab_back_material)
            setNavigationOnClickListener { finish() }
        }
        root.addView(toolbar)

        unsupportedText = TextView(this).apply {
            text = getString(R.string.ff_sounds_unsupported)
            setPadding(dp(16), dp(16), dp(16), dp(16))
            visibility = View.GONE
        }
        root.addView(unsupportedText)

        val body = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        content = body
        val collectRow = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            setPadding(dp(16), dp(8), dp(16), dp(8))
        }
        val collectTexts = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        collectTexts.addView(TextView(this).apply {
            text = getString(R.string.ff_sounds_collect)
            textSize = 16f
        })
        collectTexts.addView(TextView(this).apply {
            text = getString(R.string.ff_sounds_collect_hint)
            textSize = 12f
        })
        collectRow.addView(collectTexts, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        collectSwitch = MaterialSwitch(this).apply {
            setOnCheckedChangeListener { _, on -> if (!updatingSwitch) run("collect " + if (on) 1 else 0) }
        }
        collectRow.addView(collectSwitch)
        body.addView(collectRow)

        val buttons = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            setPadding(dp(16), 0, dp(16), dp(8))
        }
        fun button(textRes: Int, onClick: () -> Unit) =
            MaterialButton(this, null, com.google.android.material.R.attr.materialButtonOutlinedStyle).apply {
                text = getString(textRes)
                setOnClickListener { onClick() }
            }
        val lp = { LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT).apply { marginEnd = dp(8) } }
        buttons.addView(button(R.string.ff_sounds_import) { importLauncher.launch(arrayOf("*/*")) }, lp())
        buttons.addView(button(R.string.ff_sounds_export) { exportLauncher.launch(gameCode.ifEmpty { "sounds" } + ".txt") }, lp())
        buttons.addView(button(R.string.ff_sounds_clear) { run("clear") }, lp())
        body.addView(buttons)

        emptyText = TextView(this).apply {
            text = getString(R.string.ff_sounds_empty)
            setPadding(dp(16), dp(16), dp(16), dp(16))
        }
        body.addView(emptyText)
        val list = RecyclerView(this).apply {
            layoutManager = LinearLayoutManager(this@FfSoundCatalogActivity)
            adapter = this@FfSoundCatalogActivity.adapter
        }
        body.addView(list, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f))
        root.addView(body, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f))
        setContentView(root)
    }

    override fun onResume() {
        super.onResume()
        startOutput()
        handler.post(poll)
    }

    override fun onPause() {
        super.onPause()
        handler.removeCallbacks(poll)
        cmd("stop")
        stopOutput()
    }

    private fun refresh(force: Boolean) {
        val st = pairs(cmd("state"))
        val usable = st["usable"] == "1"
        unsupportedText.visibility = if (usable) View.GONE else View.VISIBLE
        content.visibility = if (usable) View.VISIBLE else View.GONE
        gameCode = st["game"].orEmpty()
        updatingSwitch = true
        collectSwitch.isChecked = st["collect"] == "1"
        updatingSwitch = false
        val v = st["version"].orEmpty()
        if (force || v != shownVersion) {
            shownVersion = v
            rows = cmd("list").lines().mapNotNull { line ->
                val p = line.split('\t')
                if (p.size < 9) return@mapNotNull null
                Row(
                    p[0], p[1].toIntOrNull() ?: 0, p[2], p[4].toIntOrNull() ?: 0, p[5].toIntOrNull() ?: -1,
                    p[6].toIntOrNull() ?: 100, p[7].toIntOrNull() ?: 0, p[8].toIntOrNull() ?: 0
                )
            }.sortedBy { it.order }
            emptyText.visibility = if (rows.isEmpty()) View.VISIBLE else View.GONE
            @Suppress("NotifyDataSetChanged")
            adapter.notifyDataSetChanged()
        }
        val ps = pairs(cmd("pstate"))
        playing = ps["playing"] == "1"
        paused = ps["paused"] == "1"
    }

    private fun kindName(kind: Int): String =
        getString(
            when (kind) {
                -2 -> R.string.ff_sounds_kind_unknown
                -1 -> R.string.ff_sounds_kind_auto
                0 -> R.string.ff_sounds_kind_none
                1 -> R.string.ff_sounds_kind_bgm
                2 -> R.string.ff_sounds_kind_jingle
                3 -> R.string.ff_sounds_kind_cutscene
                4 -> R.string.ff_sounds_kind_sfx
                else -> R.string.ff_sounds_kind_auto
            }
        )

    // 줄을 누르면: 재생/일시정지, 중지, 분류, 볼륨, 기본값으로
    private fun showActions(row: Row) {
        val isPlaying = playing && !paused && playingKey == row.key
        val items = arrayOf(
            getString(if (isPlaying) R.string.ff_sounds_pause else R.string.ff_sounds_play),
            getString(R.string.ff_sounds_stop),
            getString(R.string.ff_sounds_kind) + ": " + kindName(row.userKind),
            getString(R.string.ff_sounds_volume) + ": " + row.volume + "%",
            getString(R.string.ff_sounds_reset)
        )
        MaterialAlertDialogBuilder(this)
            .setTitle(row.name.ifEmpty { row.key })
            .setItems(items) { _, which ->
                when (which) {
                    0 -> when {
                        playing && !paused && playingKey == row.key -> run("pause 1")
                        playing && paused && playingKey == row.key -> run("pause 0")
                        else -> {
                            playingKey = row.key
                            run("play " + row.key)
                        }
                    }
                    1 -> run("stop")
                    2 -> showKinds(row)
                    3 -> showVolume(row)
                    4 -> run("reset " + row.key)
                }
            }
            .show()
    }

    private fun showKinds(row: Row) {
        val names = kinds.map { kindName(it) }.toTypedArray()
        MaterialAlertDialogBuilder(this)
            .setTitle(R.string.ff_sounds_kind)
            .setSingleChoiceItems(names, kinds.indexOf(row.userKind).coerceAtLeast(0)) { dialog, which ->
                run("kind ${row.key} ${kinds[which]}")
                dialog.dismiss()
            }
            .show()
    }

    private fun showVolume(row: Row) {
        val label = TextView(this).apply { text = "${row.volume}%" }
        val slider = Slider(this).apply {
            valueFrom = 0f
            valueTo = 200f
            stepSize = 5f
            value = (row.volume / 5 * 5).coerceIn(0, 200).toFloat()
            addOnChangeListener { _, v, _ -> label.text = "${v.toInt()}%" }
        }
        val box = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(24), dp(8), dp(24), 0)
            addView(label)
            addView(slider)
        }
        MaterialAlertDialogBuilder(this)
            .setTitle(R.string.ff_sounds_volume)
            .setView(box)
            .setPositiveButton(android.R.string.ok) { _, _ -> run("vol ${row.key} ${slider.value.toInt()}") }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    private inner class RowAdapter : RecyclerView.Adapter<RowAdapter.Holder>() {
        inner class Holder(val box: LinearLayout, val title: TextView, val detail: TextView) :
            RecyclerView.ViewHolder(box)

        override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): Holder {
            val title = TextView(parent.context).apply { textSize = 15f }
            val detail = TextView(parent.context).apply { textSize = 12f }
            val box = LinearLayout(parent.context).apply {
                orientation = LinearLayout.VERTICAL
                setPadding(dp(16), dp(10), dp(16), dp(10))
                layoutParams = RecyclerView.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.WRAP_CONTENT
                )
                val attrs = intArrayOf(android.R.attr.selectableItemBackground)
                val ta = context.obtainStyledAttributes(attrs)
                background = ta.getDrawable(0)
                ta.recycle()
                isClickable = true
                addView(title)
                addView(detail)
            }
            return Holder(box, title, detail)
        }

        override fun getItemCount() = rows.size

        override fun onBindViewHolder(holder: Holder, position: Int) {
            val row = rows[position]
            holder.title.text = row.name.ifEmpty { row.key }
            val user = if (row.userKind == -1) "-" else kindName(row.userKind)
            holder.detail.text = getString(R.string.ff_sounds_auto) + ": " + kindName(row.autoKind) + "  ·  " +
                getString(R.string.ff_sounds_kind) + ": " + user + "  ·  " +
                getString(R.string.ff_sounds_volume) + " " + row.volume + "%  ·  " +
                getString(R.string.ff_sounds_count, row.count)
            holder.box.setOnClickListener { showActions(row) }
        }
    }

    // 재생 소리: 코어가 만든 소리를 AudioTrack 으로 낸다 (화면이 열려 있는 동안)
    private fun startOutput() {
        if (outputRunning) return
        outputRunning = true
        outputThread = thread(name = "ff-bgm1x-preview") {
            val rate = 48000
            val frames = 1024
            val minBytes = AudioTrack.getMinBufferSize(rate, AudioFormat.CHANNEL_OUT_STEREO, AudioFormat.ENCODING_PCM_16BIT)
            val track = AudioTrack.Builder()
                .setAudioAttributes(
                    AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_GAME)
                        .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build()
                )
                .setAudioFormat(
                    AudioFormat.Builder().setSampleRate(rate).setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                        .setChannelMask(AudioFormat.CHANNEL_OUT_STEREO).build()
                )
                .setBufferSizeInBytes(maxOf(minBytes, frames * 4 * 2))
                .setTransferMode(AudioTrack.MODE_STREAM)
                .build()
            track.play()
            val buffer = ShortArray(frames * 2)
            while (outputRunning) {
                java.util.Arrays.fill(buffer, 0)
                runCatching { NativeLibrary.previewRender(buffer, frames, rate) }
                track.write(buffer, 0, buffer.size)
            }
            track.stop()
            track.release()
        }
    }

    private fun stopOutput() {
        outputRunning = false
        outputThread?.join(500)
        outputThread = null
    }
}
