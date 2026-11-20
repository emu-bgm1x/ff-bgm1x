package com.swordfish.lemuroid.app.mobile.feature.game

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Rect
import android.net.Uri
import java.io.File
import java.util.zip.ZipInputStream
import kotlin.math.abs

/**
 * ff-bgm1x: 다른 에뮬레이터용 스킨 묶음(zip, Pizza Boy 형식)을 읽는다.
 *
 * 묶음에는 설정 파일이 없고 그림만 있다:
 *  - bg_(portrait|landscape)_<가로>x<세로>[-변형].png : 기기 방향·화면비별 배경.
 *    게임 화면 자리는 배경에 단색 사각형으로 그려져 있어, 그 사각형을 찾아 게임 화면을 그 자리에 둔다
 *  - a.png, b.png, l.png, r.png, start.png, select.png, cross*.png ... : 버튼 그림
 */
object FfSkin {
    data class Background(val file: File, val portrait: Boolean, val ratio: Float, val variant: String)

    /** 배경이 없는 스킨(버튼 그림만)은 background·screen 이 null, 배경은 있지만 화면 자리를 못 찾으면 screen 이 null */
    data class Prepared(val dir: File, val background: File?, val screen: android.graphics.RectF?)

    private val BG_NAME = Regex("""^(?:bg_)?(portrait|landscape)_(\d+(?:\.\d+)?)x(\d+(?:\.\d+)?)(?:[_-](.+))?$""", RegexOption.IGNORE_CASE)

    /** zip 을 풀고, 방향·크기에 맞는 배경을 골라 게임 화면 자리(width x height 좌표, 배경을 늘려 채운 기준)를 찾는다 */
    fun prepare(
        context: Context,
        uri: Uri,
        portrait: Boolean,
        width: Int,
        height: Int,
        screenAspect: Float,
    ): Prepared {
        val name =
            context.contentResolver.query(uri, arrayOf(android.provider.OpenableColumns.DISPLAY_NAME), null, null, null)?.use {
                if (it.moveToFirst()) it.getString(0) else null
            } ?: "skin.zip"
        val dir = import(context, uri, name)
        val bg = pickBackground(dir, portrait, width, height) ?: return Prepared(dir, null, null)
        val ratios = listOf(screenAspect.takeIf { it > 0f } ?: 1.5f, 1.5f, 10f / 9f).distinct()
        val (size, rects) = detectScreens(bg.file, 1, ratios) ?: return Prepared(dir, bg.file, null)
        val sx = width.toFloat() / size.first
        val sy = height.toFloat() / size.second
        val r = rects[0]
        return Prepared(dir, bg.file, android.graphics.RectF(r.left * sx, r.top * sy, r.right * sx, r.bottom * sy))
    }

    private fun import(
        context: Context,
        uri: Uri,
        displayName: String,
    ): File {
        val safe = displayName.substringBeforeLast('.').replace(Regex("""[^\p{L}\p{N} ._-]"""), "_").ifBlank { "skin" }
        val dir = File(File(context.filesDir, "skins"), safe)
        dir.deleteRecursively()
        dir.mkdirs()
        val root = dir.canonicalPath
        context.contentResolver.openInputStream(uri)?.use { input ->
            ZipInputStream(input).use { zip ->
                while (true) {
                    val e = zip.nextEntry ?: break
                    val out = File(dir, e.name)
                    if (!out.canonicalPath.startsWith(root + File.separator)) continue // 묶음 밖으로 나가는 경로
                    if (e.isDirectory) {
                        out.mkdirs()
                    } else {
                        out.parentFile?.mkdirs()
                        out.outputStream().use { zip.copyTo(it) }
                    }
                }
            }
        } ?: error("cannot open")
        return dir
    }

    private fun pickBackground(
        dir: File,
        portrait: Boolean,
        width: Int,
        height: Int,
    ): Background? {
        val target = maxOf(width, height).toFloat() / minOf(width, height).coerceAtLeast(1)
        val all =
            dir.walkTopDown().filter { it.isFile && it.extension.lowercase() in setOf("png", "jpg", "jpeg") }.mapNotNull { f ->
                val m = BG_NAME.find(f.nameWithoutExtension) ?: return@mapNotNull null
                val a = m.groupValues[2].toFloatOrNull() ?: return@mapNotNull null
                val b = m.groupValues[3].toFloatOrNull() ?: return@mapNotNull null
                if (a <= 0f || b <= 0f) return@mapNotNull null
                Background(f, m.groupValues[1].equals("portrait", true), maxOf(a, b) / minOf(a, b), m.groupValues[4])
            }.toList()
        val same = all.filter { it.portrait == portrait }
        return same.filter { it.variant.isEmpty() }.ifEmpty { same }.minByOrNull { abs(it.ratio - target) }
    }

    /** 스킨의 버튼 그림 (이름: a, b, l, r, start, select, cross ...). 없으면 null */
    fun buttonImage(
        dir: File,
        name: String,
    ): File? = File(dir, "$name.png").takeIf { it.isFile }

    // ---- 배경에서 게임 화면 자리 찾기 ----

    /**
     * 배경에서 게임 화면 자리 [want] 개를 찾는다. 배경 곳곳(가로·세로 격자)의 점에서 [snapAt] 으로 사각형을 찾아,
     * 화면 비율(ratios, ±6%)이고 그림의 2% 이상인 것 중 겹치지 않는 큰 것부터 고른다.
     * (예전 방식은 많이 나오는 색으로 대충 찾아서, 어두운 몸체 위의 검은 화면처럼 색이 비슷하면 화면과 몸체가 한 덩어리가 됐다)
     */
    private fun detectScreens(
        file: File,
        want: Int,
        ratios: List<Float>,
    ): Pair<Pair<Int, Int>, List<Rect>>? {
        val full = BitmapFactory.decodeFile(file.path) ?: return null
        try {
            val small = Downscaled(full, 8)
            val found = ArrayList<Rect>()
            val cols = if (full.width > full.height) 16 else 8
            val rows = if (full.width > full.height) 8 else 16
            for (gy in 1 until rows) {
                for (gx in 1 until cols) {
                    val x = gx * full.width / cols
                    val y = gy * full.height / rows
                    if (found.any { it.contains(x, y) }) continue
                    val r = snapAt(full, small, x, y) ?: continue
                    if (r.width().toLong() * r.height() < full.width.toLong() * full.height * 0.02) continue
                    val ar = r.width().toFloat() / r.height()
                    if (ratios.none { abs(ar - it) / it < 0.06f }) continue
                    if (found.any { Rect.intersects(it, r) }) continue
                    found.add(r)
                }
            }
            if (found.size < want) return null
            return (full.width to full.height) to found.sortedWith(
                // 그림 가장자리에 닿거나 거의 닿는 것(몸체 일부가 우연히 화면 비율인 경우)은 뒤로, 나머지는 큰 것부터
                compareBy({ nearEdge(it, full) }, { -it.width().toLong() * it.height() }),
            ).take(want)
        } finally {
            full.recycle()
        }
    }

    // 사각형이 그림 가장자리에서 1.5% 안쪽까지 붙어 있는가 (진짜 화면은 가장자리에 붙지 않는다)
    private fun nearEdge(r: Rect, full: Bitmap): Boolean {
        val m = (minOf(full.width, full.height) * 0.015f).toInt()
        return r.left <= m || r.top <= m || r.right >= full.width - m || r.bottom >= full.height - m
    }

    // 원본을 [step] 배 줄인 픽셀 (여러 점에서 찾을 때 한 번만 만든다)
    private class Downscaled(full: Bitmap, val step: Int) {
        val w = (full.width / step).coerceAtLeast(1)
        val h = (full.height / step).coerceAtLeast(1)
        val px = IntArray(w * h)

        init {
            val small = Bitmap.createScaledBitmap(full, w, h, true)
            small.getPixels(px, 0, w, 0, 0, w, h)
            if (small !== full) small.recycle()
        }
    }

    /**
     * 배경 [file] 의 (x, y) 점(배경 그림 좌표)을 포함하는 화면 자리를 찾는다. 너무 작으면(128x96 미만) null.
     * 결과는 (배경 크기, 사각형). DraStic 편집기의 "화면을 짧게 눌러 맞추기"와 같은 쓰임이다.
     */
    fun detectAt(
        file: File,
        x: Int,
        y: Int,
    ): Pair<Pair<Int, Int>, Rect>? {
        val full = BitmapFactory.decodeFile(file.path) ?: return null
        try {
            val r = snapAt(full, x, y) ?: return null
            if (r.width() < 128 || r.height() < 96) return null
            return (full.width to full.height) to r
        } finally {
            full.recycle()
        }
    }

    /**
     * (x, y) 점의 색과 거의 같은 색(허용 8)으로 이뤄진, 그 점을 포함하는 가장 큰 직사각형에서 시작해
     * 네 변을 곧은 가장자리까지 넓힌다 ([growToEdges]). 색이 아니라 모양으로 끝을 정하므로, 화면 위의 대각선 반사광이나
     * 안쪽 그림자는 화면에 넣고, 화면 색에 가까운 테두리는 뺀다.
     */
    private fun snapAt(
        full: Bitmap,
        x: Int,
        y: Int,
    ): Rect? = snapAt(full, Downscaled(full, 4), x, y)

    private fun snapAt(
        full: Bitmap,
        small: Downscaled,
        x: Int,
        y: Int,
    ): Rect? {
        if (x !in 0 until full.width || y !in 0 until full.height) return null
        var sr = 0
        var sg = 0
        var sb = 0
        var n = 0
        for (dy in -2..2) for (dx in -2..2) {
            val c = full.getPixel((x + dx).coerceIn(0, full.width - 1), (y + dy).coerceIn(0, full.height - 1))
            sr += (c shr 16) and 0xFF
            sg += (c shr 8) and 0xFF
            sb += c and 0xFF
            n++
        }
        val cr = sr / n
        val cg = sg / n
        val cb = sb / n
        val w = small.w
        val h = small.h
        val step = small.step
        val px = small.px
        val tol = 8
        val mask =
            BooleanArray(w * h) { i ->
                val c = px[i]
                abs(((c shr 16) and 0xFF) - cr) <= tol && abs(((c shr 8) and 0xFF) - cg) <= tol && abs((c and 0xFF) - cb) <= tol
            }
        val r = maxRectContaining(mask, w, h, (x / step).coerceIn(0, w - 1), (y / step).coerceIn(0, h - 1)) ?: return null
        return growToEdges(full, Rect(r.left * step, r.top * step, r.right * step, r.bottom * step))
    }

    // 이진 격자에서 (sx, sy) 칸을 포함하는 가장 큰 직사각형
    private fun maxRectContaining(
        mask: BooleanArray,
        w: Int,
        h: Int,
        sx: Int,
        sy: Int,
    ): Rect? {
        if (!mask[sy * w + sx]) return null
        val heights = IntArray(w)
        var bestArea = 0
        var best: Rect? = null
        val stackX = IntArray(w + 1)
        val stackH = IntArray(w + 1)
        for (y in 0 until h) {
            for (x in 0 until w) heights[x] = if (mask[y * w + x]) heights[x] + 1 else 0
            if (y < sy) continue
            var sp = 0
            for (x in 0..w) {
                val cur = if (x < w) heights[x] else 0
                var start = x
                while (sp > 0 && stackH[sp - 1] >= cur) {
                    sp--
                    val left = stackX[sp]
                    val sh = stackH[sp]
                    val top = y - sh + 1
                    val area = sh * (x - left)
                    if (area > bestArea && left <= sx && sx < x && top <= sy) {
                        bestArea = area
                        best = Rect(left, top, x, y + 1)
                    }
                    start = left
                }
                stackX[sp] = start
                stackH[sp] = cur
                sp++
            }
        }
        return best
    }

    private const val EDGE_JUMP = 12
    private const val EDGE_SHARE = 0.6
    private const val EDGE_GAP = 4

    /**
     * 사각형의 네 변을 바깥으로 한 줄씩 넓히다가, 다음 줄이 [EDGE_GAP] 줄 안쪽과 비교해 그 줄의 [EDGE_SHARE] 이상에서
     * 색이 [EDGE_JUMP] 넘게 바뀌면(곧게 이어진 가장자리) 멈춘다. 서서히 바뀌는 그림자, 대각선 반사광은 그런 줄을 만들지 않는다.
     */
    private fun growToEdges(
        full: Bitmap,
        r: Rect,
    ): Rect {
        val w = full.width
        val h = full.height

        fun diff(
            a: Int,
            b: Int,
        ) = maxOf(
            abs(((a shr 16) and 0xFF) - ((b shr 16) and 0xFF)),
            abs(((a shr 8) and 0xFF) - ((b shr 8) and 0xFF)),
            abs((a and 0xFF) - (b and 0xFF)),
        )

        // next 줄이 ref 줄과 비교해 곧은 가장자리인가 (vertical 이면 세로줄, 아니면 가로줄)
        fun isEdge(
            next: Int,
            ref: Int,
            from: Int,
            to: Int,
            vertical: Boolean,
        ): Boolean {
            if (next < 0 || next >= (if (vertical) w else h)) return true
            var hit = 0
            var n = 0
            var v = from
            while (v <= to) {
                val a = if (vertical) full.getPixel(next, v) else full.getPixel(v, next)
                val c = if (vertical) full.getPixel(ref, v) else full.getPixel(v, ref)
                if (diff(a, c) > EDGE_JUMP) hit++
                n++
                v += 2
            }
            return hit >= n * EDGE_SHARE
        }
        // 끝을 포함하는 좌표로 다룬다
        var l = r.left.coerceIn(0, w - 1)
        var t = r.top.coerceIn(0, h - 1)
        var rr = (r.right - 1).coerceIn(l, w - 1)
        var b = (r.bottom - 1).coerceIn(t, h - 1)
        // 한 변을 넓히면 다른 변의 길이가 바뀌므로 몇 번 되풀이한다
        repeat(4) {
            while (!isEdge(l - 1, minOf(l - 1 + EDGE_GAP, rr), t, b, true)) l--
            while (!isEdge(rr + 1, maxOf(rr + 1 - EDGE_GAP, l), t, b, true)) rr++
            while (!isEdge(t - 1, minOf(t - 1 + EDGE_GAP, b), l, rr, false)) t--
            while (!isEdge(b + 1, maxOf(b + 1 - EDGE_GAP, t), l, rr, false)) b++
        }
        return Rect(l, t, rr + 1, b + 1)
    }
}
