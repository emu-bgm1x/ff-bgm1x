package com.swordfish.touchinput.radial.custom

import androidx.compose.runtime.compositionLocalOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.composed
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.layout.boundsInRoot
import androidx.compose.ui.layout.onGloballyPositioned
import gg.padkit.ids.Id
import kotlinx.serialization.Serializable
import kotlin.math.abs
import kotlin.math.min
import kotlin.math.roundToInt

// ff-bgm1x: 버튼·게임 화면 자유 배치.
// 레무로이드 원래 배치 위에 항목별 이동량(화면 크기에 대한 비율)과 크기 배율을 덧씌운다.
// 이동 0, 배율 1 이면 원래 배치 그대로다. PadKit 은 각 컨트롤의 boundsInRoot 로 터치를 판정하므로
// graphicsLayer 로 옮기고 키워도 보이는 위치와 터치 위치가 같이 움직인다.

@Serializable
data class ElementTransform(
    val dx: Float = 0f,
    val dy: Float = 0f,
    val scale: Float = 1f,
    // 세로 배율. null 이면 scale 과 같다 (게임 화면을 비율 무시하고 늘일 때만 쓴다)
    val scaleY: Float? = null,
)

@Serializable
data class CustomLayout(
    val elements: Map<String, ElementTransform> = emptyMap(),
    val showFastForwardButton: Boolean = false,
    // 꽉 채우기·가장자리 붙이기에 쓰는 여백 (px)
    val margin: Float = 0f,
    // 끌 때 맞추는 격자 간격 (px, 0 이면 끔)
    val grid: Float = 0f,
    // 게임 화면 원래 비율 유지. 끄면 게임 화면을 영역에 늘인다
    val keepAspect: Boolean = true,
    // 사용자가 만든 기능 버튼
    val customButtons: List<CustomButton> = emptyList(),
    // 그룹: 번호 → 구성원 (항목 key 또는 "g<번호>"). 그룹 안에 그룹을 넣을 수 있다
    val groups: Map<Int, List<String>> = emptyMap(),
    // 지운 기본 버튼 (화면에서 숨기고 누를 수 없게 한다. 다시 추가할 수 있다)
    val hidden: List<String> = emptyList(),
)

// 사용자가 만든 기능 버튼. function 은 CustomButtonFunction 의 code
@Serializable
data class CustomButton(
    val uid: Int,
    val function: Int,
)

// 기능 버튼이 하는 일. code 는 저장용 번호 (바꾸지 말 것)
enum class CustomButtonFunction(val code: Int) {
    NONE(0),
    MENU(1),
    FAST_FORWARD(2),
    QUICK_SAVE(3),
    QUICK_LOAD(4),
    // ff-bgm1x 1배속 옵션 켜기/끄기
    BGM_1X(5),
    SFX_1X(6),
    FF_CUTSCENES(7),
    ;

    companion object {
        fun of(code: Int) = entries.firstOrNull { it.code == code } ?: NONE

        // 화면 버튼의 키 번호. 기능을 키 번호에 넣어 두어 입력 처리에서 바로 알 수 있게 한다
        private const val KEY_BASE = 20000

        fun keyCode(button: CustomButton) = KEY_BASE + button.uid * 16 + button.function

        fun fromKeyCode(keyCode: Int): CustomButtonFunction? =
            if (keyCode >= KEY_BASE && keyCode < KEY_BASE + 16 * 1000) of((keyCode - KEY_BASE) % 16) else null
    }
}

object CustomLayoutKeys {
    const val GAME_VIEW = "game_view"
    const val FAST_FORWARD = "fast_forward"

    fun custom(uid: Int) = "custom_$uid"

    fun of(id: Id): String =
        when (id) {
            is Id.Key -> "key_${id.value}"
            is Id.DiscreteDirection -> "dir_${id.value}"
            is Id.ContinuousDirection -> "analog_${id.value}"
        }
}

enum class CustomLayoutEdge { LEFT, RIGHT, TOP, BOTTOM }

class CustomLayoutState(
    initial: CustomLayout,
) {
    var layout by mutableStateOf(initial)
    var editing by mutableStateOf(false)

    // 고른 항목들. selected 는 숫자 칸에 보이는 기준 항목 (마지막으로 누른 것)
    var selected by mutableStateOf<String?>(null)
    var selection by mutableStateOf<Set<String>>(emptySet())

    // 그 항목이 속한 맨 바깥 단위(그룹 또는 낱개) 전체를 고른다
    fun selectOnly(key: String?) {
        selected = key
        selection = if (key == null) emptySet() else leavesOf(rootOf(key)).toSet()
    }

    // 길게 누르면 그 단위를 고른 묶음에 더하거나 뺀다
    fun toggleSelection(key: String) {
        val leaves = leavesOf(rootOf(key)).toSet()
        if (key in selection) {
            selection = selection - leaves
            if (selected in leaves) selected = selection.lastOrNull()
        } else {
            selection = selection + leaves
            selected = key
        }
    }

    // 그룹 -----------------------------------------------------------------

    private fun isGroup(member: String) = member.startsWith("g")

    private fun groupId(member: String) = member.substring(1).toInt()

    private fun parentOf(member: String): Int? = layout.groups.entries.firstOrNull { member in it.value }?.key

    // 가장 바깥 그룹 (없으면 자기 자신)
    fun rootOf(member: String): String {
        var m = member
        while (true) {
            val p = parentOf(m) ?: return m
            m = "g$p"
        }
    }

    fun rootGroups(): List<String> = layout.groups.keys.filter { parentOf("g$it") == null }.map { "g$it" }

    fun leavesOf(member: String): List<String> =
        if (isGroup(member)) layout.groups[groupId(member)].orEmpty().flatMap { leavesOf(it) } else listOf(member)

    fun memberBounds(member: String): Rect? =
        leavesOf(member).mapNotNull { rectOf(it) }.reduceOrNull { a, b ->
            Rect(min(a.left, b.left), min(a.top, b.top), maxOf(a.right, b.right), maxOf(a.bottom, b.bottom))
        }

    private fun moveMember(
        member: String,
        dx: Float,
        dy: Float,
    ) {
        leavesOf(member).forEach { key -> rectOf(key)?.let { setRect(key, it.translate(dx, dy)) } }
    }

    // 고른 항목들을 덮는 맨 바깥 단위들
    fun selectedUnits(): List<String> = selection.map { rootOf(it) }.distinct()

    // 서로 맞추기·등간격의 대상: 단위가 둘 이상이면 그 단위들, 그룹 하나만 골랐으면 그 그룹의 바로 아래 구성원들
    fun alignTargets(): List<String> {
        val units = selectedUnits()
        if (units.size == 1 && isGroup(units[0])) return layout.groups[groupId(units[0])].orEmpty()
        return units
    }

    fun makeGroup() {
        val units = selectedUnits()
        if (units.size < 2) return
        val gid = (layout.groups.keys.maxOrNull() ?: 0) + 1
        layout = layout.copy(groups = layout.groups + (gid to units))
    }

    fun ungroup() {
        val doomed = selectedUnits().filter { isGroup(it) }.map { groupId(it) }.toSet()
        layout = layout.copy(groups = cleanupGroups(layout.groups - doomed))
    }

    // 빈 그룹은 지우고, 구성원이 하나뿐인 그룹은 그 구성원으로 바꾼다
    private fun cleanupGroups(input: Map<Int, List<String>>): Map<Int, List<String>> {
        val groups = input.mapValues { it.value.toMutableList() }.toMutableMap()
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
        return groups
    }

    enum class Align { LEFT, H_CENTER, RIGHT, TOP, V_CENTER, BOTTOM }

    // 서로 맞추기: 대상들의 끝이나 가운데를 같은 선에 맞춘다 (각 대상은 모양 그대로 이동)
    fun alignEach(align: Align) {
        val bounds = alignTargets().mapNotNull { m -> memberBounds(m)?.let { m to it } }
        if (bounds.size < 2) return
        val all =
            bounds.map { it.second }.reduce { a, b ->
                Rect(min(a.left, b.left), min(a.top, b.top), maxOf(a.right, b.right), maxOf(a.bottom, b.bottom))
            }
        bounds.forEach { (m, b) ->
            when (align) {
                Align.LEFT -> moveMember(m, all.left - b.left, 0f)
                Align.H_CENTER -> moveMember(m, all.center.x - b.center.x, 0f)
                Align.RIGHT -> moveMember(m, all.right - b.right, 0f)
                Align.TOP -> moveMember(m, 0f, all.top - b.top)
                Align.V_CENTER -> moveMember(m, 0f, all.center.y - b.center.y)
                Align.BOTTOM -> moveMember(m, 0f, all.bottom - b.bottom)
            }
        }
    }

    // 등간격: gap 이 null 이면 양 끝은 두고 사이를 같게, 숫자면 첫 대상부터 그 간격으로 늘어놓는다
    fun distribute(
        horizontal: Boolean,
        gap: Float?,
    ) {
        val bounds =
            alignTargets().mapNotNull { m -> memberBounds(m)?.let { m to it } }
                .sortedBy { if (horizontal) it.second.left else it.second.top }
        if (bounds.size < 2) return
        val sizes = bounds.map { if (horizontal) it.second.width else it.second.height }
        val start = if (horizontal) bounds.first().second.left else bounds.first().second.top
        val end = if (horizontal) bounds.last().second.right else bounds.last().second.bottom
        val spacing = gap ?: ((end - start - sizes.sum()) / (bounds.size - 1))
        var pos = start
        bounds.forEachIndexed { i, (m, b) ->
            if (horizontal) moveMember(m, pos - b.left, 0f) else moveMember(m, 0f, pos - b.top)
            pos += sizes[i] + spacing
        }
    }

    // 기능 버튼 ------------------------------------------------------------

    fun customButton(key: String): CustomButton? = layout.customButtons.firstOrNull { CustomLayoutKeys.custom(it.uid) == key }

    fun addCustomButton(function: CustomButtonFunction) {
        val uid = (layout.customButtons.maxOfOrNull { it.uid } ?: 0) + 1
        layout = layout.copy(customButtons = layout.customButtons + CustomButton(uid, function.code))
        selectOnly(CustomLayoutKeys.custom(uid))
    }

    fun changeCustomFunction(
        key: String,
        function: CustomButtonFunction,
    ) {
        val button = customButton(key) ?: return
        layout =
            layout.copy(
                customButtons = layout.customButtons.map { if (it.uid == button.uid) it.copy(function = function.code) else it },
            )
    }

    // 고른 버튼을 지운다. 기능 버튼은 없애고, 기본 버튼은 숨긴다 (다시 추가 목록에 나온다)
    fun deleteSelectedButtons() {
        val doomed = selection.filter { it != CustomLayoutKeys.GAME_VIEW }.toSet()
        if (doomed.isEmpty()) return
        val customs = doomed.filter { customButton(it) != null }.toSet()
        val bases = doomed - customs - CustomLayoutKeys.FAST_FORWARD
        layout =
            layout.copy(
                customButtons = layout.customButtons.filter { CustomLayoutKeys.custom(it.uid) !in customs },
                elements = layout.elements - customs,
                groups = cleanupGroups(layout.groups.mapValues { (_, v) -> v.filter { it !in doomed } }),
                hidden = (layout.hidden + bases).distinct(),
                showFastForwardButton = layout.showFastForwardButton && CustomLayoutKeys.FAST_FORWARD !in doomed,
            )
        customs.forEach { baseBounds.remove(it) }
        selection = selection - doomed
        if (selected in doomed) selected = selection.lastOrNull()
    }

    // 지운 기본 버튼을 다시 보이게 한다
    fun restoreBase(key: String) {
        layout =
            if (key == CustomLayoutKeys.FAST_FORWARD) {
                layout.copy(showFastForwardButton = true)
            } else {
                layout.copy(hidden = layout.hidden - key)
            }
        selectOnly(key)
    }

    // 추가할 수 있는 것: 지운 기본 버튼 + 화면에 없는 빨리 감기 버튼 + 아직 쓰지 않은 기능
    fun hiddenBases(): List<String> =
        layout.hidden + if (layout.showFastForwardButton) emptyList() else listOf(CustomLayoutKeys.FAST_FORWARD)

    fun unusedFunctions(): List<CustomButtonFunction> {
        val used = layout.customButtons.map { CustomButtonFunction.of(it.function) }.toSet()
        return CustomButtonFunction.entries.filter { it !in BASE_FUNCTIONS && it !in used }
    }

    // 화면 크기 (px). 이동량 비율을 픽셀로 바꿀 때 쓴다
    var screenWidth by mutableStateOf(1f)
    var screenHeight by mutableStateOf(1f)

    // 게임 화면 비율 (가로/세로). 비율 유지 중 게임 화면 크기를 맞출 때 쓴다
    var contentAspect by mutableStateOf(0f)

    // 항목별 옮기기 전 자리 (레무로이드 원래 배치). 지금 자리는 여기에 이동량·배율을 더해 구한다
    val baseBounds = mutableStateMapOf<String, Rect>()

    fun transform(key: String): ElementTransform = layout.elements[key] ?: ElementTransform()

    fun update(
        key: String,
        transform: ElementTransform,
    ) {
        layout = layout.copy(elements = layout.elements + (key to transform))
    }

    fun reset(key: String) {
        layout = layout.copy(elements = layout.elements - key)
    }

    // 항목의 지금 화면 위치 (배율은 가운데 기준)
    fun rectOf(key: String): Rect? {
        if (key in layout.hidden) return null
        val base = baseBounds[key] ?: return null
        val t = transform(key)
        val w = base.width * t.scale
        val h = base.height * (t.scaleY ?: t.scale)
        val cx = base.center.x + t.dx * screenWidth
        val cy = base.center.y + t.dy * screenHeight
        return Rect(cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2)
    }

    // 항목을 이 자리·크기에 둔다. 버튼은 너비로 배율을 정하고, 게임 화면만 높이를 따로 쓴다
    fun setRect(
        key: String,
        rect: Rect,
    ) {
        val base = baseBounds[key] ?: return
        if (base.width <= 0f || base.height <= 0f) return
        val t = transform(key)
        val scale = rect.width / base.width
        val scaleY = if (key == CustomLayoutKeys.GAME_VIEW) rect.height / base.height else null
        update(
            key,
            t.copy(
                dx = (rect.center.x - base.center.x) / screenWidth,
                dy = (rect.center.y - base.center.y) / screenHeight,
                scale = scale,
                scaleY = scaleY?.takeIf { abs(it - scale) > 1e-4f },
            ),
        )
    }

    fun moveTo(
        key: String,
        left: Float,
        top: Float,
    ) {
        val r = rectOf(key) ?: return
        setRect(key, Rect(Offset(left, top), r.size))
    }

    // 고른 묶음을 같이 옮긴다
    fun moveSelectionBy(
        dx: Float,
        dy: Float,
    ) {
        selection.forEach { key ->
            val r = rectOf(key) ?: return@forEach
            setRect(key, r.translate(dx, dy))
        }
    }

    // 기준 항목의 왼쪽 위가 (left, top) 이 되도록 고른 묶음을 옮긴다
    fun moveSelectionTo(
        left: Float,
        top: Float,
    ) {
        val r = rectOf(selected ?: return) ?: return
        moveSelectionBy(left - r.left, top - r.top)
    }

    fun selectionBounds(): Rect? =
        selection.mapNotNull { rectOf(it) }.reduceOrNull { a, b ->
            Rect(min(a.left, b.left), min(a.top, b.top), maxOf(a.right, b.right), maxOf(a.bottom, b.bottom))
        }

    // 게임 화면 크기를 바꾼다. 비율 유지 중이면 높이는 너비를 따른다
    fun resizeGameView(
        width: Float,
        height: Float,
    ) {
        val r = rectOf(CustomLayoutKeys.GAME_VIEW) ?: return
        val h = if (layout.keepAspect && contentAspect > 0f) width / contentAspect else height
        setRect(CustomLayoutKeys.GAME_VIEW, Rect(r.center.x - width / 2, r.center.y - h / 2, r.center.x + width / 2, r.center.y + h / 2))
    }

    fun snap(v: Float): Float {
        val g = layout.grid
        return if (g >= 1f) (v / g).roundToInt() * g else v
    }

    fun centerHorizontally() {
        val r = selectionBounds() ?: return
        moveSelectionBy((screenWidth - r.width) / 2 - r.left, 0f)
    }

    fun centerVertically() {
        val r = selectionBounds() ?: return
        moveSelectionBy(0f, (screenHeight - r.height) / 2 - r.top)
    }

    fun alignToEdge(edge: CustomLayoutEdge) {
        val r = selectionBounds() ?: return
        val m = layout.margin
        when (edge) {
            CustomLayoutEdge.LEFT -> moveSelectionBy(m - r.left, 0f)
            CustomLayoutEdge.RIGHT -> moveSelectionBy(screenWidth - m - r.right, 0f)
            CustomLayoutEdge.TOP -> moveSelectionBy(0f, m - r.top)
            CustomLayoutEdge.BOTTOM -> moveSelectionBy(0f, screenHeight - m - r.bottom)
        }
    }

    // 게임 화면을 여백만 남기고 꽉 채운다.
    // 세로 화면은 너비를 채우고 위에 붙이고, 가로 화면은 높이를 채우고 가운데에 둔다.
    // 비율 유지 중이면 화면 안에 들어가는 가장 큰 크기로 한다
    fun fillGameView() {
        val key = CustomLayoutKeys.GAME_VIEW
        val r = rectOf(key) ?: return
        val m = layout.margin
        val availW = screenWidth - 2 * m
        val availH = screenHeight - 2 * m
        val portrait = screenHeight > screenWidth
        val aspect = contentAspect
        val (w, h) =
            if (layout.keepAspect && aspect > 0f) {
                val w = min(availW, availH * aspect)
                w to w / aspect
            } else if (portrait) {
                availW to r.height
            } else {
                r.width to availH
            }
        val left = (screenWidth - w) / 2
        val top = if (portrait) m else (screenHeight - h) / 2
        setRect(key, Rect(left, top, left + w, top + h))
    }

    // 비율 유지를 켜면 게임 화면 영역도 게임 비율로 맞춘다 (남는 검은 띠 없이 영역 = 그림)
    fun setKeepAspect(keep: Boolean) {
        layout = layout.copy(keepAspect = keep)
        val r = rectOf(CustomLayoutKeys.GAME_VIEW)
        if (keep && r != null && layout.elements.containsKey(CustomLayoutKeys.GAME_VIEW)) {
            resizeGameView(r.width, r.height)
        }
    }

    private fun buttonKeys() = baseBounds.keys.filter { it != CustomLayoutKeys.GAME_VIEW }

    // 고른 버튼들을 기준 버튼과 같은 실제 너비(px)로 맞춘다 (자리는 가운데 기준으로 유지).
    // 버튼마다 원래 크기가 달라 배율을 따로 계산한다
    fun sameSizeForSelection() {
        val from = selected ?: return
        if (from == CustomLayoutKeys.GAME_VIEW) return
        val target = rectOf(from)?.width ?: return
        val elements = layout.elements.toMutableMap()
        selection.filter { it != CustomLayoutKeys.GAME_VIEW && it != from }.forEach { key ->
            val base = baseBounds[key] ?: return@forEach
            if (base.width <= 0f) return@forEach
            elements[key] = (elements[key] ?: ElementTransform()).copy(scale = target / base.width, scaleY = null)
        }
        layout = layout.copy(elements = elements)
    }

    // 고른 버튼 전부에 크기 배율을 준다 (게임 화면은 빼고)
    fun setScaleForSelection(scale: Float) {
        val elements = layout.elements.toMutableMap()
        selection.filter { it != CustomLayoutKeys.GAME_VIEW }.forEach { key ->
            elements[key] = (elements[key] ?: ElementTransform()).copy(scale = scale, scaleY = null)
        }
        layout = layout.copy(elements = elements)
    }

    // 왼쪽 버튼 자리를 기준으로 오른쪽 짝 버튼을 거울 자리로 옮긴다.
    // 짝은 원래 배치에서 거울 자리에 가장 가까운 오른쪽 항목이다 (L↔R, 방향키↔ABXY, SELECT↔START 등)
    fun mirrorLeftToRight() {
        val w = screenWidth
        val keys = buttonKeys()
        val left = keys.filter { baseBounds[it]!!.center.x < w / 2 }
        val right = keys.filter { baseBounds[it]!!.center.x >= w / 2 }.toMutableList()
        left.sortedBy { baseBounds[it]!!.center.y }.forEach { l ->
            val lb = baseBounds[l]!!.center
            val target = Offset(w - lb.x, lb.y)
            val r = right.minByOrNull { (baseBounds[it]!!.center - target).getDistance() } ?: return@forEach
            right.remove(r)
            val lr = rectOf(l) ?: return@forEach
            val rr = rectOf(r) ?: return@forEach
            val cx = w - lr.center.x
            val cy = lr.center.y
            setRect(r, Rect(cx - rr.width / 2, cy - rr.height / 2, cx + rr.width / 2, cy + rr.height / 2))
        }
    }

    // 편집 중 손가락 아래의 항목 (작은 것 우선, 게임 화면은 마지막)
    fun hitTest(
        x: Float,
        y: Float,
    ): String? {
        val hits = baseBounds.keys.mapNotNull { key -> rectOf(key)?.takeIf { it.contains(Offset(x, y)) }?.let { key to it } }
        return hits
            .sortedWith(compareBy({ it.first == CustomLayoutKeys.GAME_VIEW }, { it.second.width * it.second.height }))
            .firstOrNull()?.first
    }

    companion object {
        // 기본 버튼으로 이미 있는 기능 (기능 버튼 목록에서 뺀다)
        private val BASE_FUNCTIONS =
            setOf(CustomButtonFunction.NONE, CustomButtonFunction.MENU, CustomButtonFunction.FAST_FORWARD)

        const val MIN_SCALE = 0.5f
        const val MAX_SCALE = 2.0f
    }
}

val LocalCustomLayout = compositionLocalOf<CustomLayoutState?> { null }

// 컨트롤 하나(또는 게임 화면)에 자유 배치 이동·배율을 적용한다.
// 원래 자리는 graphicsLayer 보다 바깥에서 재므로 이동·배율이 들어가지 않은 값이다
fun Modifier.customLayoutElement(key: String): Modifier =
    composed {
        val state = LocalCustomLayout.current ?: return@composed this
        this
            .onGloballyPositioned { state.baseBounds[key] = it.boundsInRoot() }
            .graphicsLayer {
                val t = state.transform(key)
                // 지운 기본 버튼은 크기 0 (보이지 않고 눌리지도 않는다)
                val hidden = key in state.layout.hidden
                translationX = t.dx * state.screenWidth
                translationY = t.dy * state.screenHeight
                scaleX = if (hidden) 0f else t.scale
                scaleY = if (hidden) 0f else (t.scaleY ?: t.scale)
            }
    }
