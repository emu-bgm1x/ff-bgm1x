// ff-bgm1x: 3DS 위·아래 화면과 화면 버튼의 배치를 화면에서 끌어서 정하는 편집기.
// 사각형을 끌어 옮기고, 오른쪽 아래 모서리를 끌어 크기를 바꾸고, 숫자로도 입력한다.
// 누르면 하나(그룹이면 그룹 전체)를 고르고, 길게 누르면 여러 개를 고른다. 고른 것들은 같이 끌려 움직인다.
// 기능 버튼을 새로 만들거나 지울 수 있고, 여러 항목을 그룹으로 묶을 수 있다 (그룹 안에 그룹도 가능).
// 값은 에뮬레이션 화면(SurfaceView) 픽셀 좌표이며 Azahar 의 사용자 배치 설정·버튼 배치 설정과 같다.
package org.citra.citra_emu.display

import android.annotation.SuppressLint
import android.app.AlertDialog
import android.content.Context
import android.content.res.Configuration
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.DashPathEffect
import android.graphics.Paint
import android.graphics.RectF
import android.graphics.drawable.GradientDrawable
import android.text.Editable
import android.text.InputType
import android.text.TextWatcher
import android.view.Gravity
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.View
import android.view.ViewConfiguration
import android.view.ViewGroup
import android.widget.Button
import android.widget.CheckBox
import android.widget.EditText
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.preference.PreferenceManager
import org.citra.citra_emu.NativeLibrary.ButtonType
import org.citra.citra_emu.R
import org.citra.citra_emu.overlay.FfCustomButtons
import org.citra.citra_emu.overlay.InputOverlay
import kotlin.math.abs
import kotlin.math.hypot
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt

@SuppressLint("ViewConstructor", "ClickableViewAccessibility")
class ScreenLayoutEditor(
    context: Context,
    private val initialTop: IntArray,     // x, y, width, height
    private val initialBottom: IntArray,
    private val inputOverlay: InputOverlay,
    // 게임 화면 층이 버튼 층보다 떨어진 만큼 (화면 사각형을 이만큼 옮겨 보여 주고, 저장할 때 되돌린다)
    private val screenOffsetX: Int,
    private val screenOffsetY: Int,
    private val onSave: (IntArray, IntArray) -> Unit,
    private val onClose: () -> Unit
) : FrameLayout(context) {
    private val density = resources.displayMetrics.density
    private val preferences = PreferenceManager.getDefaultSharedPreferences(context)

    // 편집할 항목 하나 (화면 또는 버튼). key 는 그룹 저장에 쓰는 이름 (s0, s1, c<버튼 번호>)
    private class Item(
        var label: String,
        val rect: RectF,
        val screen: Int,                            // 0 = 위 화면, 1 = 아래 화면, -1 = 버튼
        val control: InputOverlay.EditableControl?,
        var scale: Int,                             // 버튼 크기 설정 (0~150, 50 = 기본)
        val baseSize: Float,                        // 버튼 크기 설정 50 일 때의 너비 (px)
        var function: FfCustomButtons.Function? = null  // 사용자가 만든 기능 버튼이면 그 기능
    ) {
        val isScreen get() = screen >= 0
        val key get() = if (isScreen) "s$screen" else "c${control!!.posId}"
    }

    private val items = ArrayList<Item>()
    private fun itemByKey(key: String) = items.firstOrNull { it.key == key }

    // 고른 항목들 (key). primary 는 숫자 칸에 보이는 항목 (마지막으로 누른 것)
    private val selection = LinkedHashSet<String>()
    private var primary: String? = null

    // 그룹: 번호 → 구성원 (항목 key 또는 "g<번호>")
    private val groups = LinkedHashMap<Int, MutableList<String>>()
    private val orientationSuffix =
        if (resources.configuration.orientation == Configuration.ORIENTATION_PORTRAIT) "-Portrait" else ""

    // 지운 기능 버튼 번호 (저장할 때 설정에서 지운다)
    private val deletedCustomIds = HashSet<Int>()

    // 기본 버튼 켜기/끄기 ("buttonToggle<번호>"). 추가하면 바로 켜서 크기·자리를 얻고, 취소하면 원래 값으로 되돌린다
    private val toggleOriginal = HashMap<Int, Boolean>()
    // 저장할 때 끌 기본 버튼 (지운 것)
    private val pendingHide = HashSet<Int>()

    private var keepAspect = true
    private var margin = preferences.getInt(PREF_MARGIN, 0)
    private var grid = preferences.getInt(PREF_GRID, 0)
    private var gap: Int? = null
    private val fields = arrayOfNulls<EditText>(4)
    private val fieldLabels = arrayOfNulls<TextView>(4)
    private val fieldColumns = arrayOfNulls<View>(4)
    private var updatingFields = false
    private var barTop = 0
    private var panelView: View? = null

    // 3DS 화면 비율 (위 400x240, 아래 320x240)
    private val aspect = floatArrayOf(400f / 240f, 320f / 240f)

    private val canvasView = object : View(context) {
        private val fill = Paint().apply { style = Paint.Style.FILL }
        private val stroke = Paint().apply {
            style = Paint.Style.STROKE
            strokeWidth = 2 * density
        }
        private val groupStroke = Paint().apply {
            style = Paint.Style.STROKE
            strokeWidth = 2 * density
            color = Color.MAGENTA
            pathEffect = DashPathEffect(floatArrayOf(8 * density, 5 * density), 0f)
        }
        private val text = Paint().apply {
            color = Color.WHITE
            isAntiAlias = true
        }

        override fun onDraw(canvas: Canvas) {
            canvas.drawColor(0x55000000)
            for (item in items) {
                val r = item.rect
                val chosen = item.key in selection
                fill.color = when {
                    item.screen == 0 -> 0x553F8EFC
                    item.screen == 1 -> 0x55FCA63F
                    item.function != null -> 0x55AB47BC
                    else -> 0x5566BB6A
                }
                canvas.drawRect(r, fill)
                stroke.color = when {
                    item.key == primary -> Color.YELLOW
                    chosen -> Color.CYAN
                    else -> Color.WHITE
                }
                canvas.drawRect(r, stroke)
                drawLabel(canvas, item)
                // 크기 조절 손잡이 (오른쪽 아래)
                val h = handleSize(item)
                fill.color = if (chosen) stroke.color else Color.WHITE
                canvas.drawRect(r.right - h, r.bottom - h, r.right, r.bottom, fill)
            }
            // 맨 바깥 그룹은 보라색 점선으로 감싼다
            val pad = 4 * density
            rootGroups().forEach { gid ->
                val b = memberBounds("g$gid") ?: return@forEach
                canvas.drawRect(b.left - pad, b.top - pad, b.right + pad, b.bottom + pad, groupStroke)
            }
        }

        // 이름이 사각형 안에 들어가도록 글자 크기를 줄인다. 화면은 왼쪽 위, 버튼은 가운데
        private fun drawLabel(canvas: Canvas, item: Item) {
            val r = item.rect
            val pad = 3 * density
            val maxW = r.width() - 2 * pad
            if (maxW <= 0f) return
            var size = min(14 * density, r.height() * 0.4f)
            text.textSize = size
            val w = text.measureText(item.label)
            if (w > maxW) {
                size *= maxW / w
                text.textSize = size
            }
            if (item.isScreen) {
                text.textAlign = Paint.Align.LEFT
                canvas.drawText(item.label, r.left + pad, r.top + pad + size, text)
            } else {
                text.textAlign = Paint.Align.CENTER
                canvas.drawText(item.label, r.centerX(), r.centerY() + size / 3, text)
            }
        }
    }

    private enum class Mode { NONE, PENDING, MOVE, RESIZE }
    private var mode = Mode.NONE
    private var downX = 0f
    private var downY = 0f
    private var lastX = 0f
    private var lastY = 0f
    private var pendingHit: String? = null
    // 끄는 중인 묶음의 기준(primary) 항목의 격자 맞춤 전 왼쪽 위
    private var rawLeft = 0f
    private var rawTop = 0f
    private val touchSlop = ViewConfiguration.get(context).scaledTouchSlop
    private val longPress = Runnable { onLongPress() }

    init {
        items.add(Item(context.getString(R.string.ff_bgm1x_layout_top_screen), screenRect(initialTop), 0, null, 0, 0f))
        items.add(Item(context.getString(R.string.ff_bgm1x_layout_bottom_screen), screenRect(initialBottom), 1, null, 0, 0f))
        inputOverlay.editableControls().forEach { c ->
            val scale = inputOverlay.controlScale(c.scaleId)
            val base = c.bounds.width() / ((scale + 50) / 100f)
            val function = inputOverlay.customFunction(c.posId)
            val label = if (function != null) context.getString(function.labelRes) else controlLabel(c.posId)
            items.add(Item(label, RectF(c.bounds), -1, c, scale, base, function))
        }
        loadGroups()
        addView(canvasView, LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT))
        canvasView.setOnTouchListener { _, e -> onCanvasTouch(e) }
        val panel = buildPanel()
        val dm = resources.displayMetrics
        val panelWidth = min(dm.widthPixels - (16 * density).roundToInt(), (520 * density).roundToInt())
        addView(panel, LayoutParams(panelWidth, LayoutParams.WRAP_CONTENT, Gravity.TOP or Gravity.CENTER_HORIZONTAL))
        // 상태 표시줄 아래부터 둔다 (맨 위를 끌면 알림창이 내려온다)
        panelView = panel
        ViewCompat.setOnApplyWindowInsetsListener(this) { _, insets ->
            barTop = insets.getInsets(WindowInsetsCompat.Type.systemBars() or WindowInsetsCompat.Type.displayCutout()).top
            requestLayout()
            insets
        }
        updateFields()
    }

    private fun controlLabel(id: Int): String = when (id) {
        ButtonType.BUTTON_A -> "A"
        ButtonType.BUTTON_B -> "B"
        ButtonType.BUTTON_X -> "X"
        ButtonType.BUTTON_Y -> "Y"
        ButtonType.BUTTON_START -> "START"
        ButtonType.BUTTON_SELECT -> "SELECT"
        ButtonType.BUTTON_HOME -> "HOME"
        ButtonType.BUTTON_ZL -> "ZL"
        ButtonType.BUTTON_ZR -> "ZR"
        ButtonType.DPAD_UP -> "D-Pad"
        ButtonType.STICK_LEFT -> "Circle"
        ButtonType.STICK_C -> "C"
        ButtonType.TRIGGER_L -> "L"
        ButtonType.TRIGGER_R -> "R"
        ButtonType.BUTTON_SWAP -> "SWAP"
        ButtonType.BUTTON_TURBO -> "TURBO"
        ButtonType.BUTTON_MENU -> "MENU"
        else -> "COMBO"
    }

    private fun screenRect(a: IntArray) = toRect(a).also { it.offset(screenOffsetX.toFloat(), screenOffsetY.toFloat()) }

    private fun screenArray(r: RectF) = toArray(RectF(r).also { it.offset(-screenOffsetX.toFloat(), -screenOffsetY.toFloat()) })

    private fun handleSize(item: Item) = min(28 * density, min(item.rect.width(), item.rect.height()) / 3)

    private fun toRect(a: IntArray) = RectF(a[0].toFloat(), a[1].toFloat(), (a[0] + a[2]).toFloat(), (a[1] + a[3]).toFloat())

    private fun toArray(r: RectF) = intArrayOf(r.left.roundToInt(), r.top.roundToInt(), r.width().roundToInt(), r.height().roundToInt())

    private fun snap(v: Float) = if (grid > 0) (v / grid).roundToInt() * grid.toFloat() else v

    private fun selectedItems() = selection.mapNotNull { itemByKey(it) }

    private fun primaryItem() = primary?.let { itemByKey(it) }

    // 그룹 ----------------------------------------------------------------

    private fun isGroup(member: String) = member.startsWith("g")

    private fun groupId(member: String) = member.substring(1).toInt()

    private fun parentOf(member: String): Int? = groups.entries.firstOrNull { member in it.value }?.key

    // 가장 바깥 그룹 (없으면 자기 자신)
    private fun rootOf(member: String): String {
        var m = member
        while (true) {
            val p = parentOf(m) ?: return m
            m = "g$p"
        }
    }

    private fun rootGroups() = groups.keys.filter { parentOf("g$it") == null }

    private fun leavesOf(member: String): List<String> =
        if (isGroup(member)) groups[groupId(member)].orEmpty().flatMap { leavesOf(it) } else listOf(member)

    private fun memberBounds(member: String): RectF? {
        val rects = leavesOf(member).mapNotNull { itemByKey(it)?.rect }
        if (rects.isEmpty()) return null
        return RectF(rects[0]).also { b -> rects.forEach { b.union(it) } }
    }

    private fun moveMember(member: String, dx: Float, dy: Float) {
        leavesOf(member).forEach { itemByKey(it)?.rect?.offset(dx, dy) }
    }

    // 고른 항목들을 덮는 맨 바깥 단위들 (그룹 또는 낱개)
    private fun selectedUnits(): List<String> = selection.map { rootOf(it) }.distinct()

    // 서로 맞추기·등간격의 대상: 단위가 둘 이상이면 그 단위들, 그룹 하나만 골랐으면 그 그룹의 바로 아래 구성원들
    private fun alignTargets(): List<String> {
        val units = selectedUnits()
        if (units.size == 1 && isGroup(units[0])) return groups[groupId(units[0])].orEmpty().toList()
        return units
    }

    private fun loadGroups() {
        groups.clear()
        val existing = items.map { it.key }.toSet()
        preferences.getString(PREF_GROUPS + orientationSuffix, "")!!.split(';').forEach { entry ->
            val parts = entry.split('=')
            if (parts.size != 2) return@forEach
            val gid = parts[0].toIntOrNull() ?: return@forEach
            groups[gid] = parts[1].split(',').filter { it.isNotEmpty() }.toMutableList()
        }
        // 지금 없는 항목은 빼고, 구성원이 하나 이하인 그룹은 푼다
        groups.values.forEach { list -> list.removeAll { !isGroup(it) && it !in existing } }
        cleanupGroups()
    }

    private fun saveGroups() {
        val text = groups.entries.joinToString(";") { "${it.key}=${it.value.joinToString(",")}" }
        preferences.edit().putString(PREF_GROUPS + orientationSuffix, text).apply()
    }

    // 빈 그룹은 지우고, 구성원이 하나뿐인 그룹은 그 구성원으로 바꾼다
    private fun cleanupGroups() {
        var changed = true
        while (changed) {
            changed = false
            for ((gid, members) in groups.entries.toList()) {
                members.removeAll { isGroup(it) && groupId(it) !in groups }
                if (members.size <= 1) {
                    val only = members.firstOrNull()
                    groups.remove(gid)
                    groups.values.forEach { list ->
                        val i = list.indexOf("g$gid")
                        if (i >= 0) {
                            if (only != null) list[i] = only else list.removeAt(i)
                        }
                    }
                    changed = true
                }
            }
        }
    }

    private fun makeGroup() {
        val units = selectedUnits()
        if (units.size < 2) return
        val gid = (groups.keys.maxOrNull() ?: 0) + 1
        groups[gid] = units.toMutableList()
        refresh()
    }

    private fun ungroup() {
        selectedUnits().filter { isGroup(it) }.forEach { groups.remove(groupId(it)) }
        cleanupGroups()
        refresh()
    }

    // 선택 ----------------------------------------------------------------

    // 그 항목이 속한 맨 바깥 단위 전체를 고른다
    private fun selectUnitOf(key: String?) {
        selection.clear()
        primary = key
        if (key != null) selection.addAll(leavesOf(rootOf(key)))
    }

    private fun toggleUnitOf(key: String) {
        val leaves = leavesOf(rootOf(key))
        if (key in selection) {
            selection.removeAll(leaves.toSet())
            if (primary in leaves) primary = selection.lastOrNull()
        } else {
            selection.addAll(leaves)
            primary = key
        }
    }

    private fun hitTest(x: Float, y: Float): String? = items
        .filter { it.rect.contains(x, y) }
        .minByOrNull { it.rect.width() * it.rect.height() }?.key

    private fun onLongPress() {
        val hit = pendingHit ?: return
        if (mode != Mode.PENDING) return
        // 길게 누르면 고른 묶음에 더하거나 뺀다
        toggleUnitOf(hit)
        canvasView.performHapticFeedback(HapticFeedbackConstants.LONG_PRESS)
        mode = Mode.NONE
        refresh()
    }

    private fun onCanvasTouch(e: MotionEvent): Boolean {
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                downX = e.x
                downY = e.y
                lastX = e.x
                lastY = e.y
                // 손잡이 먼저 (그 항목만 크기 조절)
                val hitHandle = items.firstOrNull { item ->
                    val r = item.rect
                    val h = handleSize(item)
                    e.x >= r.right - h && e.x <= r.right && e.y >= r.bottom - h && e.y <= r.bottom
                }
                if (hitHandle != null) {
                    if (hitHandle.key !in selection) selectUnitOf(hitHandle.key) else primary = hitHandle.key
                    mode = Mode.RESIZE
                    refresh()
                    return true
                }
                pendingHit = hitTest(e.x, e.y)
                mode = Mode.PENDING
                if (pendingHit != null) postDelayed(longPress, ViewConfiguration.getLongPressTimeout().toLong())
            }
            MotionEvent.ACTION_MOVE -> {
                val dx = e.x - lastX
                val dy = e.y - lastY
                lastX = e.x
                lastY = e.y
                when (mode) {
                    Mode.PENDING -> {
                        if (hypot(e.x - downX, e.y - downY) < touchSlop) return true
                        removeCallbacks(longPress)
                        val hit = pendingHit
                        if (hit == null) {
                            mode = Mode.NONE
                            return true
                        }
                        // 고른 묶음 밖의 항목을 끌면 그 단위만 고른다
                        if (hit !in selection) selectUnitOf(hit) else primary = hit
                        val r = itemByKey(hit)!!.rect
                        rawLeft = r.left + (e.x - downX)
                        rawTop = r.top + (e.y - downY)
                        mode = Mode.MOVE
                        moveTo(snap(rawLeft), snap(rawTop))
                    }
                    Mode.MOVE -> {
                        rawLeft += dx
                        rawTop += dy
                        moveTo(snap(rawLeft), snap(rawTop))
                    }
                    Mode.RESIZE -> resizePrimary(dx, dy)
                    Mode.NONE -> return true
                }
                refresh()
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                removeCallbacks(longPress)
                if (mode == Mode.PENDING && e.actionMasked == MotionEvent.ACTION_UP) {
                    // 짧게 누르면 그 단위만 고른다 (빈 곳이면 고른 것 해제)
                    selectUnitOf(pendingHit)
                    refresh()
                }
                mode = Mode.NONE
            }
        }
        return true
    }

    private fun refresh() {
        updateFields()
        canvasView.invalidate()
    }

    // 고른 항목들을 같이 옮긴다
    private fun moveSelection(dx: Float, dy: Float) {
        selectedItems().forEach { it.rect.offset(dx, dy) }
    }

    // 기준 항목의 왼쪽 위가 (left, top) 이 되도록 고른 묶음을 옮긴다
    private fun moveTo(left: Float, top: Float) {
        val p = primaryItem() ?: return
        moveSelection(left - p.rect.left, top - p.rect.top)
    }

    // 버튼 크기 설정을 바꾸고 사각형을 그 크기로 (가운데 유지)
    private fun setControlScale(item: Item, scale: Int) {
        item.scale = scale.coerceIn(0, 150)
        val r = item.rect
        val ratio = if (r.width() > 0f) r.height() / r.width() else 1f
        val w = item.baseSize * (item.scale + 50) / 100f
        val cx = r.centerX()
        val cy = r.centerY()
        r.set(cx - w / 2, cy - w * ratio / 2, cx + w / 2, cy + w * ratio / 2)
    }

    private fun resizePrimary(dx: Float, dy: Float) {
        val item = primaryItem() ?: return
        val r = item.rect
        if (item.isScreen) {
            var w = max(40f, r.width() + dx)
            var hgt = max(24f, r.height() + dy)
            if (keepAspect) {
                // 더 많이 움직인 쪽을 기준으로 비율을 맞춘다
                if (abs(dx) >= abs(dy)) hgt = w / aspect[item.screen] else w = hgt * aspect[item.screen]
            }
            r.right = r.left + w
            r.bottom = r.top + hgt
        } else {
            // 버튼은 비율 그대로, 크기 설정 단위로 바꾼다 (왼쪽 위 고정)
            val w = max(8f, r.width() + max(dx, dy))
            val left = r.left
            val top = r.top
            item.scale = (w / item.baseSize * 100f - 50f).roundToInt().coerceIn(0, 150)
            val nw = item.baseSize * (item.scale + 50) / 100f
            val ratio = r.height() / r.width()
            r.set(left, top, left + nw, top + nw * ratio)
        }
    }

    private fun updateFields() {
        updatingFields = true
        val item = primaryItem()
        val units = selectedUnits()
        selectedLabel?.text = when {
            item == null -> context.getString(R.string.ff_bgm1x_layout_hint)
            selection.size > 1 -> context.getString(R.string.ff_bgm1x_layout_selected_many, item.label, selection.size - 1)
            else -> item.label
        }
        if (item != null) {
            val a = toArray(item.rect)
            if (!item.isScreen) a[2] = item.scale + 50
            fieldLabels[2]?.text = context.getString(
                if (item.isScreen) R.string.ff_bgm1x_layout_width else R.string.ff_bgm1x_layout_size_percent
            )
            fieldColumns[3]?.visibility = if (item.isScreen) View.VISIBLE else View.GONE
            for (i in 0 until 4) {
                val f = fields[i] ?: continue
                if (!f.hasFocus()) f.setText(a[i].toString())
            }
        }
        val selItems = selectedItems()
        fillButton?.visibility = if (item?.isScreen == true && selection.size == 1) View.VISIBLE else View.GONE
        sameSizeButton?.visibility = if (selItems.count { !it.isScreen } > 1) View.VISIBLE else View.GONE
        changeFunctionButton?.visibility = if (selection.size == 1 && item?.function != null) View.VISIBLE else View.GONE
        deleteButton?.visibility = if (selItems.any { !it.isScreen }) View.VISIBLE else View.GONE
        groupButton?.visibility = if (units.size >= 2) View.VISIBLE else View.GONE
        ungroupButton?.visibility = if (units.any { isGroup(it) }) View.VISIBLE else View.GONE
        relativeSection?.visibility = if (alignTargets().size >= 2) View.VISIBLE else View.GONE
        updatingFields = false
    }

    private fun onFieldChanged(index: Int, text: String) {
        if (updatingFields) return
        val item = primaryItem() ?: return
        val v = text.toIntOrNull() ?: return
        // X·Y 는 기준 항목 자리. 고른 묶음은 모양을 유지한 채 같이 움직인다
        when (index) {
            0 -> { moveTo(v.toFloat(), item.rect.top); canvasView.invalidate(); return }
            1 -> { moveTo(item.rect.left, v.toFloat()); canvasView.invalidate(); return }
        }
        if (!item.isScreen) {
            if (index == 2) {
                // 크기 % (100 = 기본). 고른 버튼 전부에 준다 (각 버튼의 왼쪽 위 고정)
                selectedItems().filter { !it.isScreen }.forEach {
                    val left = it.rect.left
                    val top = it.rect.top
                    setControlScale(it, v - 50)
                    it.rect.offsetTo(left, top)
                }
                canvasView.invalidate()
            }
            return
        }
        val a = toArray(item.rect)
        a[index] = v
        if (keepAspect && (index == 2 || index == 3)) {
            if (index == 2) a[3] = (v / aspect[item.screen]).roundToInt() else a[2] = (v * aspect[item.screen]).roundToInt()
            updatingFields = true
            fields[if (index == 2) 3 else 2]?.setText(a[if (index == 2) 3 else 2].toString())
            updatingFields = false
        }
        if (a[2] <= 0 || a[3] <= 0) return
        item.rect.set(toRect(a))
        canvasView.invalidate()
    }

    // 화면 기준 정렬: 고른 묶음 전체를 한 덩어리로 옮긴다 -----------------------

    private fun selectionBounds(): RectF? {
        val sel = selectedItems()
        if (sel.isEmpty()) return null
        return RectF(sel[0].rect).also { b -> sel.forEach { b.union(it.rect) } }
    }

    private fun alignGroup(block: (RectF) -> Pair<Float, Float>) {
        val b = selectionBounds() ?: return
        val (dx, dy) = block(b)
        moveSelection(dx, dy)
        refresh()
    }

    private fun centerHorizontally() = alignGroup { (width - it.width()) / 2 - it.left to 0f }

    private fun centerVertically() = alignGroup { 0f to (height - it.height()) / 2 - it.top }

    private fun alignLeft() = alignGroup { margin - it.left to 0f }

    private fun alignRight() = alignGroup { width - margin - it.right to 0f }

    private fun alignTop() = alignGroup { 0f to margin - it.top }

    private fun alignBottom() = alignGroup { 0f to height - margin - it.bottom }

    // 서로 맞추기: 대상들의 끝이나 가운데를 같은 선에 맞춘다 (각 대상은 모양 그대로 이동) ------

    private enum class Edge { LEFT, H_CENTER, RIGHT, TOP, V_CENTER, BOTTOM }

    private fun alignEach(edge: Edge) {
        val targets = alignTargets()
        val bounds = targets.mapNotNull { m -> memberBounds(m)?.let { m to it } }
        if (bounds.size < 2) return
        val all = RectF(bounds[0].second).also { b -> bounds.forEach { b.union(it.second) } }
        bounds.forEach { (m, b) ->
            when (edge) {
                Edge.LEFT -> moveMember(m, all.left - b.left, 0f)
                Edge.H_CENTER -> moveMember(m, all.centerX() - b.centerX(), 0f)
                Edge.RIGHT -> moveMember(m, all.right - b.right, 0f)
                Edge.TOP -> moveMember(m, 0f, all.top - b.top)
                Edge.V_CENTER -> moveMember(m, 0f, all.centerY() - b.centerY())
                Edge.BOTTOM -> moveMember(m, 0f, all.bottom - b.bottom)
            }
        }
        refresh()
    }

    // 등간격: 간격 칸이 비어 있으면 양 끝은 두고 사이를 같게, 숫자가 있으면 첫 대상부터 그 간격으로 늘어놓는다
    private fun distribute(horizontal: Boolean) {
        val targets = alignTargets()
        val bounds = targets.mapNotNull { m -> memberBounds(m)?.let { m to it } }
            .sortedBy { if (horizontal) it.second.left else it.second.top }
        if (bounds.size < 2) return
        val sizes = bounds.map { if (horizontal) it.second.width() else it.second.height() }
        val start = if (horizontal) bounds.first().second.left else bounds.first().second.top
        val end = if (horizontal) bounds.last().second.right else bounds.last().second.bottom
        val spacing = gap?.toFloat() ?: ((end - start - sizes.sum()) / (bounds.size - 1))
        var pos = start
        bounds.forEachIndexed { i, (m, b) ->
            if (horizontal) moveMember(m, pos - b.left, 0f) else moveMember(m, 0f, pos - b.top)
            pos += sizes[i] + spacing
        }
        refresh()
    }

    // 세로 화면은 너비를, 가로 화면은 높이를 여백만 남기고 채운다 (다른 축 자리는 그대로)
    private fun fillScreen() {
        val item = primaryItem() ?: return
        if (!item.isScreen) return
        val r = item.rect
        val a = aspect[item.screen]
        if (height > width) {
            val w = (width - 2 * margin).toFloat()
            val h = if (keepAspect) w / a else r.height()
            r.set(margin.toFloat(), r.top, margin + w, r.top + h)
        } else {
            val h = (height - 2 * margin).toFloat()
            val w = if (keepAspect) h * a else r.width()
            r.set(r.left, margin.toFloat(), r.left + w, margin + h)
        }
        refresh()
    }

    // 고른 버튼들을 기준 버튼(노란 테두리)과 같은 실제 너비(px)로 맞춘다 (각 버튼의 가운데는 유지).
    // 버튼마다 기본 크기가 달라 크기 설정 값을 따로 계산한다. 설정 범위(0~150)를 넘으면 그 끝까지만 맞춘다
    private fun sameSizeForSelection() {
        val source = primaryItem() ?: return
        if (source.isScreen) return
        val target = source.rect.width()
        selectedItems().filter { !it.isScreen && it !== source && it.baseSize > 0f }.forEach {
            setControlScale(it, (target / it.baseSize * 100f - 50f).roundToInt())
        }
        refresh()
    }

    // 짝(방향키↔ABXY, L↔R, ZL↔ZR, SELECT↔START, 슬라이드패드↔C스틱) 중 왼쪽 것을 기준으로 오른쪽 것을 거울 자리로
    private fun mirrorLeftToRight() {
        val pairs = listOf(
            listOf(ButtonType.DPAD_UP) to listOf(ButtonType.BUTTON_A, ButtonType.BUTTON_B, ButtonType.BUTTON_X, ButtonType.BUTTON_Y),
            listOf(ButtonType.TRIGGER_L) to listOf(ButtonType.TRIGGER_R),
            listOf(ButtonType.BUTTON_ZL) to listOf(ButtonType.BUTTON_ZR),
            listOf(ButtonType.BUTTON_SELECT) to listOf(ButtonType.BUTTON_START),
            listOf(ButtonType.STICK_LEFT) to listOf(ButtonType.STICK_C),
        )
        fun group(ids: List<Int>) = items.filter { it.control != null && it.control.posId in ids }
        fun bbox(g: List<Item>) = RectF(g[0].rect).also { b -> g.forEach { b.union(it.rect) } }
        pairs.forEach { (a, b) ->
            val ga = group(a)
            val gb = group(b)
            if (ga.isEmpty() || gb.isEmpty()) return@forEach
            val ba = bbox(ga)
            val bb = bbox(gb)
            val (left, right, rightBox) = if (ba.centerX() <= bb.centerX()) Triple(ba, gb, bb) else Triple(bb, ga, ba)
            val dx = (width - left.centerX()) - rightBox.centerX()
            val dy = left.centerY() - rightBox.centerY()
            right.forEach { it.rect.offset(dx, dy) }
        }
        refresh()
    }

    // 버튼 추가·기능 바꾸기·지우기 ---------------------------------------------
    // 추가 목록에는 지금 화면에 버튼이 없는 기능만 나온다 (같은 기능은 하나만)

    private fun baseIndexOf(posId: Int) = BASE_POS_IDS.indexOf(posId).takeIf { it >= 0 }

    private fun usedPosIds() = items.mapNotNull { it.control?.posId }.toSet()

    // 기본 버튼 중 화면에 없는 것
    private fun missingBaseIndices() = BASE_POS_IDS.indices.filter { BASE_POS_IDS[it] !in usedPosIds() }

    // 기능 버튼 중 아직 쓰지 않은 것 (기본 버튼과 같은 기능인 메뉴·화면 전환·터보는 기본 버튼으로 넣는다)
    private fun unusedFunctions(): List<FfCustomButtons.Function> {
        val used = items.mapNotNull { it.function }.toSet()
        return FfCustomButtons.Function.entries.filter { it !in BASE_DUPLICATES && it !in used }
    }

    private fun addButton() {
        val baseNames = resources.getStringArray(R.array.n3dsButtons)
        val bases = missingBaseIndices()
        val functions = unusedFunctions()
        val names = bases.map { baseNames[it] } + functions.map { context.getString(it.labelRes) }
        if (names.isEmpty()) return
        AlertDialog.Builder(context)
            .setTitle(R.string.ff_bgm1x_layout_choose_function)
            .setItems(names.toTypedArray()) { _, which ->
                if (which < bases.size) addBaseButton(bases[which]) else addCustomButton(functions[which - bases.size])
            }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    // 기본 버튼을 다시 켠다. 켜야 Azahar 가 그 버튼의 크기·자리를 만들어 주므로 바로 켜고 목록에서 가져온다
    private fun addBaseButton(index: Int) {
        val key = "buttonToggle$index"
        if (index !in toggleOriginal) toggleOriginal[index] = preferences.getBoolean(key, false)
        preferences.edit().putBoolean(key, true).commit()
        pendingHide.remove(index)
        inputOverlay.refreshControls()
        val posId = BASE_POS_IDS[index]
        val c = inputOverlay.editableControls().firstOrNull { it.posId == posId } ?: return
        val scale = inputOverlay.controlScale(c.scaleId)
        val base = c.bounds.width() / ((scale + 50) / 100f)
        val rect = RectF(c.bounds)
        // 화면 밖에 있던 자리면 안쪽으로
        rect.offsetTo(rect.left.coerceIn(0f, max(0f, width - rect.width())), rect.top.coerceIn(0f, max(0f, height - rect.height())))
        items.add(Item(controlLabel(posId), rect, -1, c, scale, base))
        selectUnitOf("c$posId")
        refresh()
    }

    private fun addCustomButton(function: FfCustomButtons.Function) {
        val id = FfCustomButtons.nextId(usedPosIds()) ?: return
        val base = inputOverlay.customButtonBaseSize()
        val left = (width - base) / 2
        val top = (height - base) / 2
        val rect = RectF(left, top, left + base, top + base)
        val control = InputOverlay.EditableControl(id, id, true, android.graphics.Rect())
        items.add(Item(context.getString(function.labelRes), rect, -1, control, 50, base, function))
        deletedCustomIds.remove(id)
        selectUnitOf("c$id")
        refresh()
    }

    private fun changeFunction() {
        val item = primaryItem() ?: return
        if (item.function == null) return
        val functions = unusedFunctions()
        if (functions.isEmpty()) return
        AlertDialog.Builder(context)
            .setTitle(R.string.ff_bgm1x_layout_choose_function)
            .setItems(functions.map { context.getString(it.labelRes) }.toTypedArray()) { _, which ->
                item.function = functions[which]
                item.label = context.getString(functions[which].labelRes)
                refresh()
            }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    // 고른 버튼을 지운다 (기본 버튼은 저장할 때 꺼지고, 지운 기능은 다시 추가 목록에 나온다)
    private fun deleteButtons() {
        val doomed = selectedItems().filter { !it.isScreen }
        doomed.forEach { item ->
            items.remove(item)
            val posId = item.control!!.posId
            if (item.function != null) {
                deletedCustomIds.add(posId)
            } else {
                baseIndexOf(posId)?.let { pendingHide.add(it) }
            }
            groups.values.forEach { it.remove(item.key) }
            selection.remove(item.key)
        }
        cleanupGroups()
        if (primary !in selection) primary = selection.lastOrNull()
        refresh()
    }

    // 저장하지 않고 닫을 때: 추가하느라 켠 기본 버튼을 원래대로
    private fun cancel() {
        if (toggleOriginal.isNotEmpty()) {
            val edit = preferences.edit()
            toggleOriginal.forEach { (i, v) -> edit.putBoolean("buttonToggle$i", v) }
            edit.commit()
            toggleOriginal.clear()
            inputOverlay.refreshControls()
        }
        onClose()
    }

    private fun save() {
        // 기능 버튼 목록 먼저 (refreshControls 가 이 목록으로 버튼을 만든다)
        val customs = LinkedHashMap<Int, FfCustomButtons.Function>()
        items.forEach { item -> item.function?.let { customs[item.control!!.posId] = it } }
        FfCustomButtons.save(preferences, customs)
        val edit = preferences.edit()
        pendingHide.forEach { edit.putBoolean("buttonToggle$it", false) }
        toggleOriginal.clear()
        deletedCustomIds.forEach { id ->
            edit.remove("$id-X").remove("$id-Y").remove("$id-Portrait-X").remove("$id-Portrait-Y").remove("controlScale-$id")
        }
        edit.apply()
        items.forEach { item ->
            val c = item.control ?: return@forEach
            inputOverlay.saveControlPlacement(c.posId, c.scaleId, item.rect.left.roundToInt(), item.rect.top.roundToInt(), item.scale)
        }
        saveGroups()
        inputOverlay.refreshControls()
        // 화면을 건드리지 않았으면 화면 배치는 그대로 둔다 (사용자 배치로 바꾸지 않는다)
        val top = screenArray(items[0].rect)
        val bottom = screenArray(items[1].rect)
        if (!top.contentEquals(initialTop) || !bottom.contentEquals(initialBottom)) onSave(top, bottom)
    }

    // 패널 ---------------------------------------------------------------

    private var selectedLabel: TextView? = null
    private var fillButton: Button? = null
    private var sameSizeButton: Button? = null
    private var changeFunctionButton: Button? = null
    private var deleteButton: Button? = null
    private var groupButton: Button? = null
    private var ungroupButton: Button? = null
    private var relativeSection: View? = null

    // 자식들을 가로로 놓다가 폭을 넘으면 다음 줄로 넘긴다
    private class FlowLayout(context: Context, private val gap: Int) : ViewGroup(context) {
        override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
            val maxW = MeasureSpec.getSize(widthMeasureSpec)
            var x = 0
            var y = 0
            var rowH = 0
            for (i in 0 until childCount) {
                val c = getChildAt(i)
                if (c.visibility == GONE) continue
                measureChild(c, MeasureSpec.makeMeasureSpec(maxW, MeasureSpec.AT_MOST), MeasureSpec.makeMeasureSpec(0, MeasureSpec.UNSPECIFIED))
                if (x > 0 && x + c.measuredWidth > maxW) {
                    x = 0
                    y += rowH + gap
                    rowH = 0
                }
                x += c.measuredWidth + gap
                rowH = max(rowH, c.measuredHeight)
            }
            setMeasuredDimension(maxW, y + rowH)
        }

        override fun onLayout(changed: Boolean, l: Int, t: Int, r: Int, b: Int) {
            val maxW = r - l
            var x = 0
            var y = 0
            var rowH = 0
            for (i in 0 until childCount) {
                val c = getChildAt(i)
                if (c.visibility == GONE) continue
                if (x > 0 && x + c.measuredWidth > maxW) {
                    x = 0
                    y += rowH + gap
                    rowH = 0
                }
                c.layout(x, y, x + c.measuredWidth, y + c.measuredHeight)
                x += c.measuredWidth + gap
                rowH = max(rowH, c.measuredHeight)
            }
        }
    }

    // 높이가 이 값을 넘으면 스크롤한다
    private class MaxHeightScrollView(context: Context, private val maxHeight: Int) : ScrollView(context) {
        override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
            super.onMeasure(widthMeasureSpec, MeasureSpec.makeMeasureSpec(maxHeight, MeasureSpec.AT_MOST))
        }
    }

    private fun numberField(initial: String, onChanged: (String) -> Unit) = EditText(context).apply {
        inputType = InputType.TYPE_CLASS_NUMBER or InputType.TYPE_NUMBER_FLAG_SIGNED
        setTextColor(Color.WHITE)
        minEms = 3
        setText(initial)
        addTextChangedListener(object : TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
            override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {}
            override fun afterTextChanged(s: Editable?) = onChanged(s?.toString() ?: "")
        })
    }

    private fun toolButton(textRes: Int, onClick: () -> Unit) = Button(context).apply {
        text = context.getString(textRes)
        isAllCaps = false
        minWidth = 0
        minimumWidth = 0
        setPadding((10 * density).roundToInt(), paddingTop, (10 * density).roundToInt(), paddingBottom)
        setOnClickListener { onClick() }
    }

    private fun sectionTitle(textRes: Int) = TextView(context).apply {
        text = context.getString(textRes)
        setTextColor(Color.LTGRAY)
        setPadding(0, (6 * density).roundToInt(), 0, (2 * density).roundToInt())
    }

    private fun flow(vararg views: View): FlowLayout {
        val f = FlowLayout(context, (4 * density).roundToInt())
        views.forEach { f.addView(it) }
        return f
    }

    private fun buildPanel(): View {
        val pad = (8 * density).roundToInt()
        val panel = LinearLayout(context).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(pad, pad, pad, pad)
            background = GradientDrawable().apply {
                setColor(0xEE202124.toInt())
                cornerRadius = 12 * density
            }
            elevation = 8 * density
        }
        val body = LinearLayout(context).apply { orientation = LinearLayout.VERTICAL }
        val scroll = MaxHeightScrollView(context, (resources.displayMetrics.heightPixels * 0.45f).roundToInt()).apply {
            addView(body)
        }

        // 패널을 끌어 옮기는 손잡이와 접기/펴기
        val header = LinearLayout(context).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
        }
        val handle = TextView(context).apply {
            text = context.getString(R.string.ff_bgm1x_layout_panel_handle)
            setTextColor(Color.WHITE)
            setPadding(0, pad, 0, pad)
        }
        var grabX = 0f
        var grabY = 0f
        handle.setOnTouchListener { _, e ->
            when (e.actionMasked) {
                MotionEvent.ACTION_DOWN -> {
                    grabX = e.rawX - panel.translationX
                    grabY = e.rawY - panel.translationY
                }
                MotionEvent.ACTION_MOVE -> {
                    panel.translationX = e.rawX - grabX
                    panel.translationY = e.rawY - grabY
                }
            }
            true
        }
        header.addView(handle, LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f))
        header.addView(Button(context).apply {
            text = context.getString(R.string.ff_bgm1x_layout_collapse)
            isAllCaps = false
            setOnClickListener {
                val collapse = scroll.visibility == View.VISIBLE
                scroll.visibility = if (collapse) View.GONE else View.VISIBLE
                text = context.getString(if (collapse) R.string.ff_bgm1x_layout_expand else R.string.ff_bgm1x_layout_collapse)
            }
        })
        panel.addView(header)
        panel.addView(scroll)

        selectedLabel = TextView(context).apply { setTextColor(Color.YELLOW) }
        body.addView(selectedLabel)

        val row = FlowLayout(context, pad)
        val names = arrayOf("X", "Y", context.getString(R.string.ff_bgm1x_layout_width), context.getString(R.string.ff_bgm1x_layout_height))
        for (i in 0 until 4) {
            val col = LinearLayout(context).apply { orientation = LinearLayout.VERTICAL }
            val label = TextView(context).apply {
                text = names[i]
                setTextColor(Color.LTGRAY)
            }
            fieldLabels[i] = label
            col.addView(label)
            val f = numberField("") { onFieldChanged(i, it) }
            fields[i] = f
            col.addView(f)
            fieldColumns[i] = col
            row.addView(col)
        }
        body.addView(row)

        // 버튼 만들기·그룹
        changeFunctionButton = toolButton(R.string.ff_bgm1x_layout_change_function) { changeFunction() }
        deleteButton = toolButton(R.string.ff_bgm1x_layout_delete) { deleteButtons() }
        groupButton = toolButton(R.string.ff_bgm1x_layout_group) { makeGroup() }
        ungroupButton = toolButton(R.string.ff_bgm1x_layout_ungroup) { ungroup() }
        sameSizeButton = toolButton(R.string.ff_bgm1x_layout_same_size) { sameSizeForSelection() }
        body.addView(flow(
            toolButton(R.string.ff_bgm1x_layout_add_button) { addButton() },
            changeFunctionButton!!,
            deleteButton!!,
            groupButton!!,
            ungroupButton!!,
            sameSizeButton!!,
        ))

        // 서로 맞추기 (단위가 둘 이상이거나, 그룹 하나를 골랐을 때 그 안에서)
        val relative = LinearLayout(context).apply { orientation = LinearLayout.VERTICAL }
        relative.addView(sectionTitle(R.string.ff_bgm1x_layout_section_each))
        relative.addView(flow(
            toolButton(R.string.ff_bgm1x_layout_align_left) { alignEach(Edge.LEFT) },
            toolButton(R.string.ff_bgm1x_layout_align_hcenter) { alignEach(Edge.H_CENTER) },
            toolButton(R.string.ff_bgm1x_layout_align_right) { alignEach(Edge.RIGHT) },
            toolButton(R.string.ff_bgm1x_layout_align_top) { alignEach(Edge.TOP) },
            toolButton(R.string.ff_bgm1x_layout_align_vcenter) { alignEach(Edge.V_CENTER) },
            toolButton(R.string.ff_bgm1x_layout_align_bottom) { alignEach(Edge.BOTTOM) },
            toolButton(R.string.ff_bgm1x_layout_dist_h) { distribute(true) },
            toolButton(R.string.ff_bgm1x_layout_dist_v) { distribute(false) },
        ))
        relative.addView(LinearLayout(context).apply {
            orientation = LinearLayout.VERTICAL
            addView(TextView(context).apply {
                text = context.getString(R.string.ff_bgm1x_layout_gap)
                setTextColor(Color.LTGRAY)
            })
            addView(numberField("") { s -> gap = s.toIntOrNull()?.let { max(0, it) } })
        })
        relativeSection = relative
        body.addView(relative)

        // 화면 기준 정렬
        fillButton = toolButton(R.string.ff_bgm1x_layout_fill) { fillScreen() }
        body.addView(sectionTitle(R.string.ff_bgm1x_layout_section_screen))
        body.addView(flow(
            toolButton(R.string.ff_bgm1x_layout_center_h) { centerHorizontally() },
            toolButton(R.string.ff_bgm1x_layout_center_v) { centerVertically() },
            toolButton(R.string.ff_bgm1x_layout_edge_left) { alignLeft() },
            toolButton(R.string.ff_bgm1x_layout_edge_right) { alignRight() },
            toolButton(R.string.ff_bgm1x_layout_edge_top) { alignTop() },
            toolButton(R.string.ff_bgm1x_layout_edge_bottom) { alignBottom() },
            fillButton!!,
            toolButton(R.string.ff_bgm1x_layout_mirror) { mirrorLeftToRight() },
            toolButton(R.string.ff_bgm1x_layout_fix_aspect) {
                val item = primaryItem()
                if (item != null && item.isScreen) {
                    item.rect.bottom = item.rect.top + item.rect.width() / aspect[item.screen]
                    refresh()
                }
            },
        ))

        // 여백·격자 (편집기 설정, 앱 설정에 저장)
        val settingsRow = FlowLayout(context, pad)
        fun settingColumn(labelRes: Int, value: Int, onValue: (Int) -> Unit): View {
            val col = LinearLayout(context).apply { orientation = LinearLayout.VERTICAL }
            col.addView(TextView(context).apply {
                text = context.getString(labelRes)
                setTextColor(Color.LTGRAY)
            })
            col.addView(numberField(value.toString()) { s -> s.toIntOrNull()?.let { onValue(max(0, it)) } })
            return col
        }
        settingsRow.addView(settingColumn(R.string.ff_bgm1x_layout_margin, margin) {
            margin = it
            preferences.edit().putInt(PREF_MARGIN, it).apply()
        })
        settingsRow.addView(settingColumn(R.string.ff_bgm1x_layout_grid, grid) {
            grid = it
            preferences.edit().putInt(PREF_GRID, it).apply()
        })
        body.addView(settingsRow)

        body.addView(CheckBox(context).apply {
            text = context.getString(R.string.ff_bgm1x_layout_keep_aspect)
            setTextColor(Color.WHITE)
            isChecked = keepAspect
            setOnCheckedChangeListener { _, checked -> keepAspect = checked }
        })

        // 저장·취소는 스크롤 밖에 두어 항상 보이게 한다
        val buttons = LinearLayout(context).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.END
        }
        buttons.addView(Button(context).apply {
            text = context.getString(android.R.string.cancel)
            setOnClickListener { cancel() }
        })
        buttons.addView(Button(context).apply {
            text = context.getString(R.string.ff_bgm1x_layout_save)
            setOnClickListener {
                save()
                onClose()
            }
        })
        panel.addView(buttons)
        return panel
    }

    // 상태 표시줄 높이. 편집기 자체가 노치 여백만큼 내려와 있으면 그만큼은 빼고 패널을 띄운다
    // 화면 밖으로 나간 버튼은 처음 열 때 안쪽으로 넣어 보이게 한다
    private var clampedOnce = false

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        super.onSizeChanged(w, h, oldw, oldh)
        if (clampedOnce || w == 0 || h == 0) return
        clampedOnce = true
        items.filter { !it.isScreen }.forEach { item ->
            val r = item.rect
            val x = r.left.coerceIn(0f, max(0f, w - r.width()))
            val y = r.top.coerceIn(0f, max(0f, h - r.height()))
            r.offsetTo(x, y)
        }
        canvasView.invalidate()
    }

    override fun onLayout(changed: Boolean, left: Int, top: Int, right: Int, bottom: Int) {
        super.onLayout(changed, left, top, right, bottom)
        val panel = panelView ?: return
        val loc = IntArray(2)
        getLocationInWindow(loc)
        val want = max(0, barTop - loc[1])
        val lp = panel.layoutParams as LayoutParams
        if (lp.topMargin != want) {
            lp.topMargin = want
            post { panel.requestLayout() }
        }
    }

    // 나중에 붙인 뷰라 여백 정보를 다시 받아야 상태 표시줄 높이가 들어온다
    override fun onAttachedToWindow() {
        super.onAttachedToWindow()
        ViewCompat.requestApplyInsets(this)
    }

    // 저장하지 않고 닫는다 (뒤로 버튼)
    fun close() = cancel()

    companion object {
        private const val PREF_MARGIN = "ffLayoutEditorMargin"
        private const val PREF_GRID = "ffLayoutEditorGrid"
        private const val PREF_GROUPS = "ffLayoutGroups"

        // "buttonToggle<번호>" 의 번호 순서대로, 그 기본 버튼의 자리 설정 번호
        private val BASE_POS_IDS = listOf(
            ButtonType.BUTTON_A, ButtonType.BUTTON_B, ButtonType.BUTTON_X, ButtonType.BUTTON_Y,
            ButtonType.TRIGGER_L, ButtonType.TRIGGER_R, ButtonType.BUTTON_ZL, ButtonType.BUTTON_ZR,
            ButtonType.BUTTON_START, ButtonType.BUTTON_SELECT, ButtonType.DPAD_UP, ButtonType.STICK_LEFT,
            ButtonType.STICK_C, ButtonType.BUTTON_HOME, ButtonType.BUTTON_SWAP, ButtonType.BUTTON_TURBO,
            org.citra.citra_emu.features.hotkeys.Hotkey.COMBO_BUTTON.button, ButtonType.BUTTON_MENU,
        )

        private val BASE_DUPLICATES = setOf(
            FfCustomButtons.Function.NONE,
            FfCustomButtons.Function.MENU,
            FfCustomButtons.Function.SWAP_SCREEN,
            FfCustomButtons.Function.TURBO,
        )
    }
}
