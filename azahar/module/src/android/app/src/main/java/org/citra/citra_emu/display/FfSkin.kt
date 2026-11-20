// ff-bgm1x: 다른 에뮬레이터용 스킨(zip, Citra 형식)을 버튼 층에 입힌다.
//
// 묶음에는 설정 파일이 없고 그림만 있다:
//  - backgrounds/bg_(portrait|landscape)_<가로>x<세로>[_변형].jpg : 기기 방향·화면비별 배경.
//    게임 화면 자리는 배경에 단색 사각형으로 그려져 있어, 그 사각형을 찾아 게임 화면을 그 자리에 둔다
//  - skin/<버튼>.png, <버튼>_pressed.png : 버튼 그림 (a, b, x, y, l, r, zl, zr, start, select, home, dpad, joystick, c_stick ...)
// 배경은 버튼 층 맨 아래에 그리고, 게임 화면 자리는 비워 둔다 (게임 화면 층이 버튼 층 아래에 있으므로).
package org.citra.citra_emu.display

import android.content.Context
import android.content.SharedPreferences
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.Rect
import android.graphics.RectF
import android.net.Uri
import android.os.Build
import java.io.File
import java.util.zip.ZipInputStream
import kotlin.math.abs

object FfSkin {
    private const val PREF_DIR = "ffSkinDir"        // + 방향 접미사: 그 방향에 쓰는 스킨 폴더 (접미사 없는 값은 예전 판의 공통 값)
    private const val PREF_BG = "ffSkinBg"       // + 방향 접미사 ("-Portrait" 또는 "")
    private const val PREF_HOLES = "ffSkinHoles" // + 방향 접미사: 게임 화면 자리 (버튼 층 좌표) "l,t,r,b;l,t,r,b"

    data class Background(val file: File, val portrait: Boolean, val ratio: Float, val variant: String)

    private val BG_NAME = Regex("""^(?:bg_)?(portrait|landscape)_(\d+(?:\.\d+)?)x(\d+(?:\.\d+)?)(?:_(.+))?$""", RegexOption.IGNORE_CASE)

    // 버튼 그림 이름 (앱 그림 이름 -> 스킨 파일 이름)
    private val BUTTON_FILES = mapOf(
        "button_a" to "a", "button_a_pressed" to "a_pressed",
        "button_b" to "b", "button_b_pressed" to "b_pressed",
        "button_x" to "x", "button_x_pressed" to "x_pressed",
        "button_y" to "y", "button_y_pressed" to "y_pressed",
        "button_l" to "l", "button_l_pressed" to "l_pressed",
        "button_r" to "r", "button_r_pressed" to "r_pressed",
        "button_zl" to "zl", "button_zl_pressed" to "zl_pressed",
        "button_zr" to "zr", "button_zr_pressed" to "zr_pressed",
        "button_start" to "start", "button_start_pressed" to "start_pressed",
        "button_select" to "select", "button_select_pressed" to "select_pressed",
        "button_home" to "home", "button_home_pressed" to "home_pressed",
        "dpad" to "dpad", "dpad_pressed_one_direction" to "dpad_pressed_one",
        "dpad_pressed_two_directions" to "dpad_pressed_two",
        "stick_main_range" to "joystick_range", "stick_main" to "joystick", "stick_main_pressed" to "joystick_pressed",
        "stick_c_range" to "c_stick_range", "stick_c" to "c_stick", "stick_c_pressed" to "c_stick_pressed",
    )

    fun orientationSuffix(portrait: Boolean) = if (portrait) "-Portrait" else ""

    /** 그 방향에 쓰는 스킨 폴더. 세로·가로를 따로 둔다 (예전 판의 공통 값이 남아 있으면 그것) */
    fun skinDir(prefs: SharedPreferences, portrait: Boolean): File? =
        (prefs.getString(PREF_DIR + dirSuffix(portrait), null) ?: prefs.getString(PREF_DIR, null))
            ?.let { File(it) }?.takeIf { it.isDirectory }

    // 스킨 폴더 키 접미사 (배경 키와 같은 규칙을 쓰면 가로 키가 예전 공통 키와 겹치므로 따로)
    private fun dirSuffix(portrait: Boolean) = if (portrait) "-Portrait" else "-Landscape"

    // 예전 판의 공통 스킨 폴더 값을 세로·가로 값으로 옮긴다 (없는 쪽에만)
    private fun migrateDir(prefs: SharedPreferences, e: SharedPreferences.Editor) {
        val old = prefs.getString(PREF_DIR, null) ?: return
        for (p in listOf(true, false)) {
            if (prefs.getString(PREF_DIR + dirSuffix(p), null) == null) e.putString(PREF_DIR + dirSuffix(p), old)
        }
        e.remove(PREF_DIR)
    }

    // ---- 가져오기 ----

    /** zip 을 앱 내부 폴더(files/skins/<이름>)에 푼다 */
    fun import(context: Context, uri: Uri, displayName: String): File {
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
                    if (!out.canonicalPath.startsWith(root + File.separator)) continue  // 묶음 밖으로 나가는 경로
                    if (e.isDirectory) {
                        out.mkdirs()
                    } else {
                        out.parentFile?.mkdirs()
                        out.outputStream().use { zip.copyTo(it) }
                    }
                }
            }
        } ?: error("cannot open")
        // DraStic 스킨은 버튼 그림이 안쪽 zip(virtual_controller/<이름>.zip)에 들어 있다: 같은 이름 폴더로 푼다
        dir.walkTopDown().filter { it.isFile && it.extension.equals("zip", true) }.toList().forEach { inner ->
            val out = File(inner.parentFile, inner.nameWithoutExtension)
            out.mkdirs()
            val innerRoot = out.canonicalPath
            inner.inputStream().use { input ->
                ZipInputStream(input).use { zip ->
                    while (true) {
                        val e = zip.nextEntry ?: break
                        val f = File(out, e.name)
                        if (!f.canonicalPath.startsWith(innerRoot + File.separator)) continue
                        if (e.isDirectory) {
                            f.mkdirs()
                        } else {
                            f.parentFile?.mkdirs()
                            f.outputStream().use { zip.copyTo(it) }
                        }
                    }
                }
            }
        }
        return dir
    }

    fun backgrounds(dir: File): List<Background> =
        dir.walkTopDown().filter { it.isFile && it.extension.lowercase() in setOf("png", "jpg", "jpeg") }.mapNotNull { f ->
            val m = BG_NAME.find(f.nameWithoutExtension) ?: return@mapNotNull null
            val a = m.groupValues[2].toFloatOrNull() ?: return@mapNotNull null
            val b = m.groupValues[3].toFloatOrNull() ?: return@mapNotNull null
            if (a <= 0f || b <= 0f) return@mapNotNull null
            Background(f, m.groupValues[1].equals("portrait", true), maxOf(a, b) / minOf(a, b), m.groupValues[4])
        }.toList()

    /** 방향·화면비에 가장 가까운 기본 배경 (변형 없는 것 우선) */
    fun pickBackground(dir: File, portrait: Boolean, width: Int, height: Int): Background? {
        val target = maxOf(width, height).toFloat() / minOf(width, height).coerceAtLeast(1)
        val same = backgrounds(dir).filter { it.portrait == portrait }
        return same.filter { it.variant.isEmpty() }.ifEmpty { same }.minByOrNull { abs(it.ratio - target) }
    }

    /**
     * 스킨 zip 을 풀고, 방향·크기(width x height, 버튼 층 크기)에 맞는 배경을 골라 게임 화면 자리를 찾는다.
     * 결과: (스킨 폴더, 배경 파일, 위 화면, 아래 화면) — 화면 자리는 버튼 층 좌표 (배경을 늘려 채운 기준)
     * 배경이 없는 스킨(버튼 그림만 있는 것)은 background·top·bottom 이 null, 배경은 있지만 화면 자리를 못 찾으면 top·bottom 이 null.
     */
    data class Prepared(val dir: File, val background: File?, val top: RectF?, val bottom: RectF?)

    fun prepare(context: Context, uri: Uri, portrait: Boolean, width: Int, height: Int): Prepared {
        val name = context.contentResolver.query(uri, arrayOf(android.provider.OpenableColumns.DISPLAY_NAME), null, null, null)?.use {
            if (it.moveToFirst()) it.getString(0) else null
        } ?: "skin.zip"
        val dir = import(context, uri, name)
        val bg = pickBackground(dir, portrait, width, height) ?: return Prepared(dir, null, null, null)
        val (size, rects) = detectScreens(bg.file, 2, listOf(400f / 240f, 320f / 240f))
            ?: return Prepared(dir, bg.file, null, null)
        val sx = width.toFloat() / size.first
        val sy = height.toFloat() / size.second
        fun map(r: Rect) = RectF(r.left * sx, r.top * sy, r.right * sx, r.bottom * sy)
        fun ratio(r: Rect) = r.width().toFloat() / r.height()
        val a = rects[0]
        val b = rects[1]
        // 두 자리의 비율이 뚜렷이 다르면(3DS 스킨: 5:3 과 4:3) 5:3 에 가까운 쪽이 위 화면.
        // 비율이 같으면(DS 스킨: 둘 다 4:3) 위쪽(같은 높이면 왼쪽)에 있는 쪽이 위 화면.
        val ratioClear = abs(ratio(a) - ratio(b)) / maxOf(ratio(a), ratio(b)) > 0.1f
        val (topRect, bottomRect) = if (ratioClear) {
            if (abs(ratio(a) - 400f / 240f) <= abs(ratio(b) - 400f / 240f)) a to b else b to a
        } else {
            val aFirst = if (abs(a.top - b.top) > minOf(a.height(), b.height()) / 2) a.top < b.top else a.left <= b.left
            if (aFirst) a to b else b to a
        }
        var top = map(topRect)
        // DS 스킨처럼 위 화면 자리가 5:3 이 아니면, 3DS 위 화면(5:3)을 그 자리 안에 찌그러뜨리지 않고 가운데에 맞춘다
        if (!ratioClear) top = fitInside(top, 400f / 240f)
        return Prepared(dir, bg.file, top, map(bottomRect))
    }

    // [r] 안에 비율 [aspect] 인 가장 큰 사각형 (가운데)
    private fun fitInside(r: RectF, aspect: Float): RectF {
        val cur = r.width() / r.height()
        if (abs(cur - aspect) / aspect < 0.03f) return r
        return if (cur < aspect) {
            val h = r.width() / aspect
            val y = r.top + (r.height() - h) / 2
            RectF(r.left, y, r.right, y + h)
        } else {
            val w = r.height() * aspect
            val x = r.left + (r.width() - w) / 2
            RectF(x, r.top, x + w, r.bottom)
        }
    }

    // ---- 저장된 스킨 ----

    /**
     * [portrait] 방향에만 스킨을 저장한다 (세로·가로는 서로 다른 스킨이어도 된다).
     * [bg] 가 null 이면 버튼 그림만 쓰고, 그 방향에 전에 깔린 배경은 지운다.
     */
    fun save(prefs: SharedPreferences, dir: File, portrait: Boolean, bg: File?, top: RectF, bottom: RectF) {
        val s = orientationSuffix(portrait)
        val e = prefs.edit()
        migrateDir(prefs, e)
        e.putString(PREF_DIR + dirSuffix(portrait), dir.path)
        if (bg == null) {
            e.remove(PREF_BG + s).remove(PREF_HOLES + s)
        } else {
            e.putString(PREF_BG + s, bg.path).putString(PREF_HOLES + s, holes(top, bottom))
        }
        e.apply()
    }

    /** 스킨이 있는 방향에서 화면 자리를 옮겼으면 비워 둘 자리도 옮긴다 */
    fun updateHoles(prefs: SharedPreferences, portrait: Boolean, top: RectF, bottom: RectF) {
        val s = orientationSuffix(portrait)
        if (prefs.getString(PREF_BG + s, null) == null) return
        prefs.edit().putString(PREF_HOLES + s, holes(top, bottom)).apply()
    }

    /** [portrait] 방향의 스킨만 끈다 */
    fun clear(prefs: SharedPreferences, portrait: Boolean) {
        val s = orientationSuffix(portrait)
        val e = prefs.edit()
        migrateDir(prefs, e)
        e.remove(PREF_DIR + dirSuffix(portrait)).remove(PREF_BG + s).remove(PREF_HOLES + s)
        e.apply()
        cachedBg = null
        cachedBgPath = null
    }

    private fun holes(top: RectF, bottom: RectF) =
        listOf(top, bottom).joinToString(";") { "${it.left.toInt()},${it.top.toInt()},${it.right.toInt()},${it.bottom.toInt()}" }

    // ---- 그리기 (버튼 층) ----

    private var cachedBg: Bitmap? = null
    private var cachedBgPath: String? = null

    /** 배경을 버튼 층 전체에 늘려 그리고, 게임 화면 자리는 비워 둔다 */
    fun drawBackground(canvas: Canvas, prefs: SharedPreferences, portrait: Boolean, width: Int, height: Int) {
        val s = orientationSuffix(portrait)
        val path = prefs.getString(PREF_BG + s, null) ?: return
        if (path != cachedBgPath) {
            cachedBg?.recycle()
            cachedBg = BitmapFactory.decodeFile(path)
            cachedBgPath = path
        }
        val bmp = cachedBg ?: return
        canvas.save()
        prefs.getString(PREF_HOLES + s, null)?.split(';')?.forEach { h ->
            val v = h.split(',').mapNotNull { it.toIntOrNull() }
            if (v.size == 4) {
                val r = Rect(v[0], v[1], v[2], v[3])
                if (Build.VERSION.SDK_INT >= 26) canvas.clipOutRect(r)
                else @Suppress("DEPRECATION") canvas.clipRect(r, android.graphics.Region.Op.DIFFERENCE)
            }
        }
        canvas.drawBitmap(bmp, null, Rect(0, 0, width, height), null)
        canvas.restore()
    }

    /** 앱 버튼 그림 대신 쓸 스킨 그림 (없으면 null) */
    fun buttonBitmap(context: Context, prefs: SharedPreferences, drawableId: Int): Bitmap? {
        val dir = activeDir(context, prefs) ?: return null
        val name = runCatching { context.resources.getResourceEntryName(drawableId) }.getOrNull() ?: return null
        skinImage(dir, name)?.let { return it }
        // 스킨에 ZL·ZR 그림이 없으면 L·R 그림의 모양에 ZL·ZR 글자
        if (name.startsWith("button_zl") || name.startsWith("button_zr")) {
            val src = skinImage(dir, name.replace("button_z", "button_")) ?: return null
            return labeledShape(src, if (name.startsWith("button_zl")) "ZL" else "ZR")
        }
        // 스킨에 스틱 그림이 없으면 앱 기본 그림을 스킨 버튼들의 평균 색으로
        if (name.startsWith("stick_")) {
            val color = averageButtonColor(dir) ?: return null
            return tintedDefault(context, drawableId, color)
        }
        return null
    }

    /** 지금 방향에 스킨이 켜져 있으면 그 폴더 */
    fun activeDir(context: Context, prefs: SharedPreferences): File? {
        val portrait = context.resources.configuration.orientation == android.content.res.Configuration.ORIENTATION_PORTRAIT
        return skinDir(prefs, portrait)
    }

    private fun skinImage(dir: File, name: String): Bitmap? {
        val file = BUTTON_FILES[name] ?: return drasticButton(dir, name)
        val f = dir.walkTopDown().firstOrNull { it.isFile && it.nameWithoutExtension.equals(file, true) && it.extension.equals("png", true) }
            ?: return drasticButton(dir, name)
        return BitmapFactory.decodeFile(f.path)
    }

    /**
     * [src] 의 윤곽(알파)만 남기고 그 그림에서 가장 많이 쓴 색(반투명한 점 제외)으로 채운 뒤 가운데에 [label].
     * 앱이 버튼 그림을 정사각형으로 늘리므로 글자는 미리 가로로 w/h 만큼 늘려 그려 늘린 뒤 바른 비율이 되게 한다.
     */
    private fun labeledShape(src: Bitmap, label: String): Bitmap {
        val w = src.width; val h = src.height
        val px = IntArray(w * h)
        src.getPixels(px, 0, w, 0, 0, w, h)
        // 가장 많이 쓴 색: 채널마다 위 4비트로 묶어 세고, 그 묶음의 평균
        val count = HashMap<Int, IntArray>()
        for (c in px) {
            if ((c ushr 24) < 128) continue
            val key = ((c shr 20) and 0xF shl 8) or ((c shr 12) and 0xF shl 4) or ((c shr 4) and 0xF)
            val s = count.getOrPut(key) { IntArray(4) }
            s[0]++; s[1] += (c shr 16) and 0xFF; s[2] += (c shr 8) and 0xFF; s[3] += c and 0xFF
        }
        val top = count.values.maxByOrNull { it[0] }
        val fill = if (top == null) 0xFFEAEAEA.toInt()
            else (0xFF shl 24) or ((top[1] / top[0]) shl 16) or ((top[2] / top[0]) shl 8) or (top[3] / top[0])
        val rgb = fill and 0xFFFFFF
        for (i in px.indices) px[i] = (px[i] and 0xFF000000.toInt()) or rgb
        val out = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888)
        out.setPixels(px, 0, w, 0, 0, w, h)
        // 글자: 채운 색이 밝으면 어두운 글자, 어두우면 흰 글자
        val lum = 0.299 * ((fill shr 16) and 0xFF) + 0.587 * ((fill shr 8) and 0xFF) + 0.114 * (fill and 0xFF)
        val paint = android.graphics.Paint(android.graphics.Paint.ANTI_ALIAS_FLAG).apply {
            color = if (lum > 140) 0xBF000000.toInt() else 0xBFFFFFFF.toInt()
            typeface = android.graphics.Typeface.DEFAULT_BOLD
            textAlign = android.graphics.Paint.Align.CENTER
            textScaleX = w.toFloat() / h
            textSize = h * 0.42f
        }
        val tw = paint.measureText(label)
        if (tw > w * 0.6f) paint.textSize *= w * 0.6f / tw
        val fm = paint.fontMetrics
        Canvas(out).drawText(label, w / 2f, h / 2f - (fm.ascent + fm.descent) / 2f, paint)
        return out
    }

    private val averageCache = HashMap<String, Int?>()

    /** 스킨 버튼 그림들(누르지 않은 그림)의 평균 색. 반투명한 점(알파 128 미만)은 빼고, 알파로 가중 */
    private fun averageButtonColor(dir: File): Int? = synchronized(averageCache) {
        averageCache.getOrPut(dir.path) {
            val pics = BUTTON_FILES.keys.filter { !it.contains("pressed") && !it.startsWith("stick_") }
                .mapNotNull { name ->
                    val file = BUTTON_FILES[name]!!
                    dir.walkTopDown().firstOrNull { it.isFile && it.nameWithoutExtension.equals(file, true) && it.extension.equals("png", true) }
                } + listOfNotNull(drasticImage(dir, "padbuttons_0"), drasticImage(dir, "dpad_0"))
            var r = 0.0; var g = 0.0; var b = 0.0; var w = 0.0
            for (f in pics.distinct()) {
                val opts = BitmapFactory.Options().apply { inSampleSize = 4 }
                val bmp = BitmapFactory.decodeFile(f.path, opts) ?: continue
                val px = IntArray(bmp.width * bmp.height)
                bmp.getPixels(px, 0, bmp.width, 0, 0, bmp.width, bmp.height)
                for (c in px) {
                    val a = c ushr 24
                    if (a < 128) continue
                    r += ((c shr 16) and 0xFF) * a; g += ((c shr 8) and 0xFF) * a; b += (c and 0xFF) * a; w += a
                }
                bmp.recycle()
            }
            if (w == 0.0) null
            else (0xFF shl 24) or ((r / w).toInt() shl 16) or ((g / w).toInt() shl 8) or (b / w).toInt()
        }
    }

    /** 앱 기본 그림의 밝기를 [color] 로 물들인다 (흰 곳 = color, 검은 곳 = 검정, 알파는 그대로) */
    private fun tintedDefault(context: Context, drawableId: Int, color: Int): Bitmap? {
        val d = androidx.core.content.ContextCompat.getDrawable(context, drawableId) ?: return null
        val side = 512
        val src = Bitmap.createBitmap(side, side, Bitmap.Config.ARGB_8888)
        d.setBounds(0, 0, side, side)
        d.draw(Canvas(src))
        val cr = ((color shr 16) and 0xFF) / 255f
        val cg = ((color shr 8) and 0xFF) / 255f
        val cb = (color and 0xFF) / 255f
        val m = android.graphics.ColorMatrix(floatArrayOf(
            0.299f * cr, 0.587f * cr, 0.114f * cr, 0f, 0f,
            0.299f * cg, 0.587f * cg, 0.114f * cg, 0f, 0f,
            0.299f * cb, 0.587f * cb, 0.114f * cb, 0f, 0f,
            0f, 0f, 0f, 1f, 0f,
        ))
        val out = Bitmap.createBitmap(side, side, Bitmap.Config.ARGB_8888)
        val paint = android.graphics.Paint().apply { colorFilter = android.graphics.ColorMatrixColorFilter(m) }
        Canvas(out).drawBitmap(src, 0f, 0f, paint)
        src.recycle()
        return out
    }

    // ---- DraStic 스킨의 버튼 (melonDS 와 같은 형식) ----
    // virtual_controller/<이름>/ 안에 padbuttons_N.png(버튼 그림판, 0 = 보통, 1 = 누름)와 dpad_N.png(십자키)가 있다.
    // 그림판은 DraStic 이 정해진 칸(그림판 크기에 대한 비율: 왼쪽, 위, 오른쪽, 아래)을 잘라 쓰는 형식이다.
    private val DRASTIC_CELLS = mapOf(
        "x" to floatArrayOf(0f, 0.5f, 0.25f, 1f),
        "y" to floatArrayOf(0.25f, 0.5f, 0.5f, 1f),
        "b" to floatArrayOf(0.25f, 0f, 0.5f, 0.5f),
        "a" to floatArrayOf(0f, 0f, 0.25f, 0.5f),
        "r" to floatArrayOf(0.500977f, 0.253906f, 0.875f, 0.5f),
        "l" to floatArrayOf(0.500977f, 0.00390625f, 0.875f, 0.25f),
        "start" to floatArrayOf(0.500977f, 0.503906f, 0.75f, 0.75f),
        "select" to floatArrayOf(0.500977f, 0.753906f, 0.75f, 1f),
    )

    // 보통 판이 없으면 작은(_low) 판
    private fun drasticImage(dir: File, base: String): File? {
        val files = dir.walkTopDown().filter { it.isFile && it.path.contains("virtual_controller") }.toList()
        return files.firstOrNull { it.name.equals("$base.png", true) } ?: files.firstOrNull { it.name.equals("${base}_low.png", true) }
    }

    /**
     * 앱 그림 이름에 맞는 DraStic 버튼 그림. 십자키는 Azahar 가 "위" 눌림 그림과 "위·왼쪽" 눌림 그림을 돌려 쓰므로
     * DraStic 의 dpad_1(위)·dpad_8(왼쪽 위)을 쓴다. DraStic 에 없는 버튼(ZL·ZR·HOME·스틱)은 null (buttonBitmap 과 InputOverlay 가 대신 그린다).
     */
    private fun drasticButton(dir: File, drawableName: String): Bitmap? {
        when (drawableName) {
            "dpad" -> return drasticImage(dir, "dpad_0")?.let { BitmapFactory.decodeFile(it.path) }
            "dpad_pressed_one_direction" -> return drasticImage(dir, "dpad_1")?.let { BitmapFactory.decodeFile(it.path) }
            "dpad_pressed_two_directions" -> return drasticImage(dir, "dpad_8")?.let { BitmapFactory.decodeFile(it.path) }
        }
        val pressed = drawableName.endsWith("_pressed")
        val key = drawableName.removePrefix("button_").removeSuffix("_pressed")
        val cell = DRASTIC_CELLS[key] ?: return null
        val sheetFile = (if (pressed) drasticImage(dir, "padbuttons_1") else null) ?: drasticImage(dir, "padbuttons_0") ?: return null
        val sheet = BitmapFactory.decodeFile(sheetFile.path) ?: return null
        val r = Rect(
            (cell[0] * sheet.width).toInt(),
            (cell[1] * sheet.height).toInt(),
            (cell[2] * sheet.width).toInt().coerceAtMost(sheet.width),
            (cell[3] * sheet.height).toInt().coerceAtMost(sheet.height),
        )
        if (r.width() <= 0 || r.height() <= 0) return null
        return Bitmap.createBitmap(sheet, r.left, r.top, r.width(), r.height())
    }

    // ---- 배경에서 게임 화면 자리 찾기 ----

    /**
     * 배경에서 게임 화면 자리 [want] 개를 찾는다. 배경 곳곳(가로·세로 격자)의 점에서 [snapAt] 으로 사각형을 찾아,
     * 화면 비율(ratios, ±6%)이고 그림의 2% 이상인 것 중 겹치지 않는 큰 것부터 고른다.
     * (예전 방식은 많이 나오는 색으로 대충 찾아서, 어두운 몸체 위의 검은 화면처럼 색이 비슷하면 화면과 몸체가 한 덩어리가 됐다)
     * 결과: (그림 크기, 찾은 사각형들) 또는 null
     */
    fun detectScreens(file: File, want: Int, ratios: List<Float>): Pair<Pair<Int, Int>, List<Rect>>? {
        val full = BitmapFactory.decodeFile(file.path) ?: return null
        try {
            val small = Downscaled(full, 8)
            val found = ArrayList<Rect>()
            val cols = if (full.width > full.height) 16 else 8
            val rows = if (full.width > full.height) 8 else 16
            for (gy in 1 until rows) for (gx in 1 until cols) {
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
            if (found.size < want) return null
            val best = found.sortedWith(
                // 그림 가장자리에 닿거나 거의 닿는 것(몸체 일부가 우연히 화면 비율인 경우)은 뒤로, 나머지는 큰 것부터
                compareBy({ nearEdge(it, full) }, { -it.width().toLong() * it.height() }),
            ).take(want)
            return (full.width to full.height) to best
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

    /** 그 방향에 깔린 스킨 배경 그림 (없으면 null) */
    fun backgroundFile(prefs: SharedPreferences, portrait: Boolean): File? =
        prefs.getString(PREF_BG + orientationSuffix(portrait), null)?.let { File(it) }?.takeIf { it.isFile }

    /**
     * 배경 [file] 의 (x, y) 점(배경 그림 좌표)을 포함하는 화면 자리를 찾는다. 너무 작으면(128x96 미만) null.
     * 결과는 (배경 크기, 사각형). DraStic 편집기의 "화면을 짧게 눌러 맞추기"와 같은 쓰임이다.
     */
    fun detectAt(file: File, x: Int, y: Int): Pair<Pair<Int, Int>, Rect>? {
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
    private fun snapAt(full: Bitmap, x: Int, y: Int): Rect? = snapAt(full, Downscaled(full, 4), x, y)

    private fun snapAt(full: Bitmap, small: Downscaled, x: Int, y: Int): Rect? {
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
        val mask = BooleanArray(w * h) { i ->
            val c = px[i]
            abs(((c shr 16) and 0xFF) - cr) <= tol && abs(((c shr 8) and 0xFF) - cg) <= tol && abs((c and 0xFF) - cb) <= tol
        }
        val r = maxRectContaining(mask, w, h, (x / step).coerceIn(0, w - 1), (y / step).coerceIn(0, h - 1)) ?: return null
        return growToEdges(full, Rect(r.left * step, r.top * step, r.right * step, r.bottom * step))
    }

    // 이진 격자에서 (sx, sy) 칸을 포함하는 가장 큰 직사각형
    private fun maxRectContaining(mask: BooleanArray, w: Int, h: Int, sx: Int, sy: Int): Rect? {
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
    private fun growToEdges(full: Bitmap, r: Rect): Rect {
        val w = full.width
        val h = full.height
        fun diff(a: Int, b: Int) = maxOf(
            abs(((a shr 16) and 0xFF) - ((b shr 16) and 0xFF)),
            abs(((a shr 8) and 0xFF) - ((b shr 8) and 0xFF)),
            abs((a and 0xFF) - (b and 0xFF)),
        )
        // next 줄이 ref 줄과 비교해 곧은 가장자리인가 (vertical 이면 세로줄, 아니면 가로줄)
        fun isEdge(next: Int, ref: Int, from: Int, to: Int, vertical: Boolean): Boolean {
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
