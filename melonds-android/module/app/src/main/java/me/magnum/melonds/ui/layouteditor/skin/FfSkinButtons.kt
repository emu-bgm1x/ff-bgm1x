package me.magnum.melonds.ui.layouteditor.skin

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.Rect
import java.io.File
import java.util.UUID

/**
 * ff-bgm1x: DraStic 스킨의 버튼 그림.
 *
 * virtual_controller/<이름>/ 안에 (이름 규칙 `(dpad|padbuttons)_<번호>[_low].png`, `_low` 는 작은 판):
 *  - dpad_N.png : 십자키. 0 = 누르지 않음, 1 = 위부터 시계 방향으로 누른 모양
 *    (1 위, 2 오른쪽위, 3 오른쪽, 4 오른쪽아래, 5 아래, 6 왼쪽아래, 7 왼쪽, 8 왼쪽위)
 *  - padbuttons_N.png : 버튼 그림판. 0 = 누르지 않음, 1 = 누른 모양
 * 그림판은 DraStic 이 정해진 칸(그림판 크기에 대한 비율)을 잘라 쓰는 형식이라 같은 칸을 잘라 쓴다.
 */
class FfSkinButtons(private val dir: File) {
    private val atlas: Bitmap? by lazy { load("padbuttons_0") }
    val dpad: Bitmap? by lazy { load("dpad_0") }

    /** 보통 판을 먼저, 없으면 `_low` 판을 읽는다 */
    private fun load(base: String): Bitmap? {
        val files = dir.walkTopDown().filter { it.isFile }.toList()
        val f = files.firstOrNull { it.name.equals("$base.png", true) } ?: files.firstOrNull { it.name.equals("${base}_low.png", true) }
        return f?.let { BitmapFactory.decodeFile(it.path) }
    }

    /** 그림판에서 비율 칸 (왼쪽, 위, 오른쪽, 아래) 을 잘라 낸다 */
    private fun crop(cell: FloatArray): Bitmap? {
        val a = atlas ?: return null
        val r = Rect(
            (cell[0] * a.width).toInt(),
            (cell[1] * a.height).toInt(),
            (cell[2] * a.width).toInt().coerceAtMost(a.width),
            (cell[3] * a.height).toInt().coerceAtMost(a.height),
        )
        if (r.width() <= 0 || r.height() <= 0) return null
        return Bitmap.createBitmap(a, r.left, r.top, r.width(), r.height())
    }

    val a by lazy { crop(CELL_A) }
    val b by lazy { crop(CELL_B) }
    val x by lazy { crop(CELL_X) }
    val y by lazy { crop(CELL_Y) }
    val l by lazy { crop(CELL_L) }
    val r by lazy { crop(CELL_R) }
    val start by lazy { crop(CELL_START) }
    val select by lazy { crop(CELL_SELECT) }

    /** A·B·X·Y 를 DS 배치(위 X, 왼쪽 Y, 오른쪽 A, 아래 B)로 한 장에 모은다 */
    val faceButtons: Bitmap? by lazy {
        val ax = a ?: return@lazy null
        val bx = b ?: return@lazy null
        val xx = x ?: return@lazy null
        val yx = y ?: return@lazy null
        val c = ax.width
        val size = c * 3
        val out = Bitmap.createBitmap(size, size, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(out)
        canvas.drawBitmap(xx, c.toFloat(), 0f, null)
        canvas.drawBitmap(yx, 0f, c.toFloat(), null)
        canvas.drawBitmap(ax, (c * 2).toFloat(), c.toFloat(), null)
        canvas.drawBitmap(bx, c.toFloat(), (c * 2).toFloat(), null)
        out
    }

    companion object {
        // DraStic 이 그림판을 자르는 칸 (그림판 너비·높이에 대한 비율: 왼쪽, 위, 오른쪽, 아래)
        private val CELL_X = floatArrayOf(0f, 0.5f, 0.25f, 1f)
        private val CELL_Y = floatArrayOf(0.25f, 0.5f, 0.5f, 1f)
        private val CELL_B = floatArrayOf(0.25f, 0f, 0.5f, 0.5f)
        private val CELL_A = floatArrayOf(0f, 0f, 0.25f, 0.5f)
        private val CELL_R = floatArrayOf(0.500977f, 0.253906f, 0.875f, 0.5f)
        private val CELL_L = floatArrayOf(0.500977f, 0.00390625f, 0.875f, 0.25f)
        private val CELL_START = floatArrayOf(0.500977f, 0.503906f, 0.75f, 0.75f)
        private val CELL_SELECT = floatArrayOf(0.500977f, 0.753906f, 0.75f, 1f)

        private const val PREFS = "ff_skin"

        /** 지금 그리는 레이아웃의 스킨 버튼 (레이아웃을 만들 때 정한다) */
        @Volatile var current: FfSkinButtons? = null

        private val cache = HashMap<String, FfSkinButtons>()

        /** 지금 레이아웃의 스킨 배경 그림 (늘려 채우기일 때만). 편집기에서 화면을 눌러 맞출 때 쓴다 */
        @Volatile var currentBackground: File? = null

        /** 스킨 배경과 그 스킨 폴더·배경 그림을 이어 둔다 (레이아웃의 배경으로 스킨을 알아본다) */
        fun register(context: Context, backgroundId: UUID, dir: File, background: File) {
            context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
                .putString(backgroundId.toString(), dir.path)
                .putString("$backgroundId.bg", background.path)
                .apply()
        }

        fun select(context: Context, backgroundId: UUID?, stretched: Boolean) {
            val prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            val path = backgroundId?.let { prefs.getString(it.toString(), null) }
            current = path?.let { p -> File(p).takeIf { it.isDirectory }?.let { cache.getOrPut(p) { FfSkinButtons(it) } } }
            currentBackground = if (stretched) backgroundId?.let { prefs.getString("$it.bg", null) }?.let { File(it) }?.takeIf { it.isFile } else null
        }
    }
}
