package com.swordfish.lemuroid.app.mobile.feature.game

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Card
import androidx.compose.material3.FilterChip
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Slider
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.layout.positionInRoot
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import com.swordfish.lemuroid.R
import com.swordfish.touchinput.radial.custom.CustomButtonFunction
import com.swordfish.touchinput.radial.custom.CustomLayout
import com.swordfish.touchinput.radial.custom.CustomLayoutEdge
import com.swordfish.touchinput.radial.custom.CustomLayoutKeys
import com.swordfish.touchinput.radial.custom.CustomLayoutState
import kotlin.math.roundToInt

// ff-bgm1x: 버튼·게임 화면 자유 배치 편집 화면.
// 항목을 끌어 옮기고, 고른 항목의 자리·크기를 숫자(px)로 바꾸고, 정렬·꽉 채우기·대칭 등을 한다.
// 저장은 "기본" 또는 "이 게임".
@OptIn(ExperimentalLayoutApi::class)
@Composable
fun CustomLayoutEditor(
    state: CustomLayoutState,
    initialForGame: Boolean,
    onSave: (CustomLayout, Boolean) -> Unit,
    onClear: (Boolean) -> Unit,
    onClose: () -> Unit,
) {
    var forGame by remember { mutableStateOf(initialForGame) }
    var origin by remember { mutableStateOf(Offset.Zero) }
    var panelOffset by remember { mutableStateOf(Offset.Zero) }
    var collapsed by remember { mutableStateOf(false) }
    // 끄는 중인 항목의 격자 맞춤 전 왼쪽 위 (root 좌표)
    var dragPos by remember { mutableStateOf(Offset.Zero) }
    // 기능 고르기 창 (열려 있으면 고른 뒤 부를 함수). includeBases 면 지운 기본 버튼도 목록에 넣는다
    var choosingFunction by remember { mutableStateOf<((CustomButtonFunction) -> Unit)?>(null) }
    var includeBases by remember { mutableStateOf(false) }

    choosingFunction?.let { onChosen ->
        AlertDialog(
            onDismissRequest = { choosingFunction = null },
            title = { Text(stringResource(R.string.custom_layout_choose_function)) },
            text = {
                Column(modifier = Modifier.heightIn(max = 360.dp).verticalScroll(rememberScrollState())) {
                    if (includeBases) {
                        state.hiddenBases().forEach { key ->
                            TextButton(
                                modifier = Modifier.fillMaxWidth(),
                                onClick = {
                                    choosingFunction = null
                                    state.restoreBase(key)
                                },
                            ) {
                                Text(modifier = Modifier.fillMaxWidth(), text = baseButtonName(key))
                            }
                        }
                    }
                    state.unusedFunctions().forEach { function ->
                        TextButton(
                            modifier = Modifier.fillMaxWidth(),
                            onClick = {
                                choosingFunction = null
                                onChosen(function)
                            },
                        ) {
                            Text(modifier = Modifier.fillMaxWidth(), text = customFunctionName(function))
                        }
                    }
                }
            },
            confirmButton = {},
            dismissButton = {
                TextButton(onClick = { choosingFunction = null }) { Text(stringResource(android.R.string.cancel)) }
            },
        )
    }

    Box(modifier = Modifier.fillMaxSize()) {
        // 끌기 판정 (버튼에는 터치가 가지 않는다)
        Box(
            modifier =
                Modifier
                    .fillMaxSize()
                    .background(Color(0x33000000))
                    .onGloballyPositioned { origin = it.positionInRoot() }
                    // 짧게 누르면 그것 하나만, 길게 누르면 고른 묶음에 더하거나 뺀다
                    .pointerInput(Unit) {
                        detectTapGestures(
                            onTap = { pos ->
                                val p = pos + origin
                                state.selectOnly(state.hitTest(p.x, p.y))
                            },
                            onLongPress = { pos ->
                                val p = pos + origin
                                state.hitTest(p.x, p.y)?.let { state.toggleSelection(it) }
                            },
                        )
                    }
                    .pointerInput(Unit) {
                        detectDragGestures(
                            onDragStart = { start ->
                                val p = start + origin
                                val hit = state.hitTest(p.x, p.y)
                                // 고른 묶음 안의 항목을 끌면 묶음째, 밖이면 그것만 옮긴다
                                if (hit == null || hit !in state.selection) state.selectOnly(hit) else state.selected = hit
                                dragPos = state.selected?.let { state.rectOf(it)?.topLeft } ?: Offset.Zero
                            },
                            onDrag = { change, amount ->
                                change.consume()
                                if (state.selected == null) return@detectDragGestures
                                dragPos += amount
                                state.moveSelectionTo(
                                    state.snap(dragPos.x - origin.x) + origin.x,
                                    state.snap(dragPos.y - origin.y) + origin.y,
                                )
                            },
                        )
                    },
        )

        // 항목 테두리 (기준 항목은 노란색, 함께 고른 항목은 하늘색)
        Canvas(modifier = Modifier.fillMaxSize()) {
            state.baseBounds.keys.forEach { key ->
                val rect = state.rectOf(key) ?: return@forEach
                val chosen = key in state.selection
                drawRect(
                    color =
                        when {
                            key == state.selected -> Color.Yellow
                            chosen -> Color.Cyan
                            else -> Color.White.copy(alpha = 0.6f)
                        },
                    topLeft = Offset(rect.left - origin.x, rect.top - origin.y),
                    size = Size(rect.width, rect.height),
                    style = Stroke(width = if (chosen) 4.dp.toPx() else 1.5.dp.toPx()),
                )
            }
            // 맨 바깥 그룹은 보라색 점선으로 감싼다
            val pad = 4.dp.toPx()
            state.rootGroups().forEach { group ->
                val b = state.memberBounds(group) ?: return@forEach
                drawRect(
                    color = Color.Magenta,
                    topLeft = Offset(b.left - origin.x - pad, b.top - origin.y - pad),
                    size = Size(b.width + 2 * pad, b.height + 2 * pad),
                    style =
                        Stroke(
                            width = 2.dp.toPx(),
                            pathEffect = PathEffect.dashPathEffect(floatArrayOf(8.dp.toPx(), 5.dp.toPx())),
                        ),
                )
            }
        }

        Card(
            modifier =
                Modifier
                    .align(Alignment.TopCenter)
                    // 상태 표시줄 아래부터 둔다 (맨 위를 끌면 알림창이 내려온다)
                    .windowInsetsPadding(WindowInsets.safeDrawing)
                    .offset { IntOffset(panelOffset.x.roundToInt(), panelOffset.y.roundToInt()) }
                    .padding(8.dp)
                    .widthIn(max = 460.dp),
        ) {
            Column(
                modifier = Modifier.padding(8.dp),
                verticalArrangement = Arrangement.spacedBy(4.dp),
            ) {
                // 패널 자체를 끌어 옮기는 손잡이와 접기/펴기
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(
                        modifier =
                            Modifier
                                .weight(1f)
                                .pointerInput(Unit) {
                                    detectDragGestures { change, amount ->
                                        change.consume()
                                        panelOffset += amount
                                    }
                                }
                                .padding(vertical = 8.dp),
                        text = stringResource(R.string.custom_layout_panel_handle),
                    )
                    TextButton(onClick = { collapsed = !collapsed }) {
                        Text(
                            stringResource(
                                if (collapsed) R.string.custom_layout_panel_expand else R.string.custom_layout_panel_collapse,
                            ),
                        )
                    }
                }
                if (collapsed) return@Column

                // 내용이 길어 가로 화면에서도 다 보이도록 스크롤한다
                Column(
                    modifier =
                        Modifier
                            .heightIn(max = 280.dp)
                            .verticalScroll(rememberScrollState()),
                    verticalArrangement = Arrangement.spacedBy(4.dp),
                ) {
                    SelectedItemControls(state, origin)
                    ButtonAndGroupControls(state) { withBases, onChosen ->
                        includeBases = withBases
                        choosingFunction = onChosen
                    }
                    if (state.alignTargets().size >= 2) RelativeAlignControls(state)
                    LayoutControls(state)
                }

                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    FilterChip(
                        selected = !forGame,
                        onClick = { forGame = false },
                        label = { Text(stringResource(R.string.custom_layout_target_default)) },
                    )
                    FilterChip(
                        selected = forGame,
                        onClick = { forGame = true },
                        label = { Text(stringResource(R.string.custom_layout_target_game)) },
                    )
                }
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.End,
                ) {
                    TextButton(
                        onClick = {
                            onClear(forGame)
                            onClose()
                        },
                    ) {
                        Text(stringResource(R.string.custom_layout_reset_all))
                    }
                    TextButton(onClick = onClose) {
                        Text(stringResource(android.R.string.cancel))
                    }
                    TextButton(
                        onClick = {
                            onSave(state.layout, forGame)
                            onClose()
                        },
                    ) {
                        Text(stringResource(R.string.custom_layout_save))
                    }
                }
            }
        }
    }
}

// 고른 항목: 자리·크기 숫자, 정렬, 꽉 채우기, 같은 크기
@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun SelectedItemControls(
    state: CustomLayoutState,
    origin: Offset,
) {
    val selected = state.selected
    val name =
        when (selected) {
            null -> stringResource(R.string.custom_layout_hint)
            CustomLayoutKeys.GAME_VIEW -> stringResource(R.string.custom_layout_game_view)
            CustomLayoutKeys.FAST_FORWARD -> stringResource(R.string.game_menu_fast_forward)
            else -> stringResource(R.string.custom_layout_selected_button)
        }
    Text(
        text =
            if (selected != null && state.selection.size > 1) {
                stringResource(R.string.custom_layout_selected_many, name, state.selection.size - 1)
            } else {
                name
            },
    )
    if (selected == null) return
    val rect = state.rectOf(selected) ?: return
    val isGameView = selected == CustomLayoutKeys.GAME_VIEW
    val t = state.transform(selected)

    if (!isGameView) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(text = stringResource(R.string.custom_layout_size, (t.scale * 100).roundToInt()))
            Slider(
                modifier = Modifier.weight(1f).padding(start = 8.dp),
                value = t.scale.coerceIn(CustomLayoutState.MIN_SCALE, CustomLayoutState.MAX_SCALE),
                valueRange = CustomLayoutState.MIN_SCALE..CustomLayoutState.MAX_SCALE,
                onValueChange = { state.setScaleForSelection(it) },
            )
        }
    }

    // 숫자로 정확히: X·Y 는 화면 왼쪽 위에서 항목 왼쪽 위까지의 픽셀
    Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
        NumberField(
            modifier = Modifier.weight(1f),
            label = stringResource(R.string.custom_layout_x),
            fieldKey = "$selected/x",
            value = rect.left - origin.x,
            decimals = false,
            onValue = { x -> state.rectOf(selected)?.let { state.moveSelectionTo(x + origin.x, it.top) } },
        )
        NumberField(
            modifier = Modifier.weight(1f),
            label = stringResource(R.string.custom_layout_y),
            fieldKey = "$selected/y",
            value = rect.top - origin.y,
            decimals = false,
            onValue = { y -> state.rectOf(selected)?.let { state.moveSelectionTo(it.left, y + origin.y) } },
        )
        if (isGameView) {
            NumberField(
                modifier = Modifier.weight(1f),
                label = stringResource(R.string.custom_layout_w),
                fieldKey = "$selected/w",
                value = rect.width,
                decimals = false,
                onValue = { w -> state.rectOf(selected)?.let { if (w >= 1f) state.resizeGameView(w, it.height) } },
            )
            NumberField(
                modifier = Modifier.weight(1f),
                label = stringResource(R.string.custom_layout_h),
                fieldKey = "$selected/h",
                value = rect.height,
                decimals = false,
                onValue = { h ->
                    state.rectOf(selected)?.let {
                        if (h < 1f) return@let
                        // 비율 유지 중에는 높이에 맞는 너비로 바꾼다
                        val a = state.contentAspect
                        if (state.layout.keepAspect && a > 0f) state.resizeGameView(h * a, h) else state.resizeGameView(it.width, h)
                    }
                },
            )
        } else {
            NumberField(
                modifier = Modifier.weight(1f),
                label = stringResource(R.string.custom_layout_size_field),
                fieldKey = "$selected/s",
                value = t.scale * 100f,
                onValue = {
                    state.setScaleForSelection((it / 100f).coerceIn(CustomLayoutState.MIN_SCALE, CustomLayoutState.MAX_SCALE))
                },
            )
        }
    }

    FlowRow(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
        SmallButton(R.string.custom_layout_center_h) { state.centerHorizontally() }
        SmallButton(R.string.custom_layout_center_v) { state.centerVertically() }
        SmallButton(R.string.custom_layout_edge_left) { state.alignToEdge(CustomLayoutEdge.LEFT) }
        SmallButton(R.string.custom_layout_edge_right) { state.alignToEdge(CustomLayoutEdge.RIGHT) }
        SmallButton(R.string.custom_layout_edge_top) { state.alignToEdge(CustomLayoutEdge.TOP) }
        SmallButton(R.string.custom_layout_edge_bottom) { state.alignToEdge(CustomLayoutEdge.BOTTOM) }
        if (isGameView && state.selection.size == 1) {
            SmallButton(R.string.custom_layout_fill) { state.fillGameView() }
        }
        if (state.selection.count { it != CustomLayoutKeys.GAME_VIEW } > 1) {
            SmallButton(R.string.custom_layout_same_size) { state.sameSizeForSelection() }
        }
        SmallButton(R.string.custom_layout_reset_item) { state.selection.forEach { state.reset(it) } }
    }
}

// 기능 버튼 만들기·바꾸기·지우기, 그룹 만들기·풀기
@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun ButtonAndGroupControls(
    state: CustomLayoutState,
    chooseFunction: (Boolean, (CustomButtonFunction) -> Unit) -> Unit,
) {
    val selected = state.selected
    val units = state.selectedUnits()
    FlowRow(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
        if (state.hiddenBases().isNotEmpty() || state.unusedFunctions().isNotEmpty()) {
            SmallButton(R.string.custom_layout_add_button) { chooseFunction(true) { state.addCustomButton(it) } }
        }
        if (selected != null && state.selection.size == 1 && state.customButton(selected) != null && state.unusedFunctions().isNotEmpty()) {
            SmallButton(R.string.custom_layout_change_function) { chooseFunction(false) { state.changeCustomFunction(selected, it) } }
        }
        if (state.selection.any { it != CustomLayoutKeys.GAME_VIEW }) {
            SmallButton(R.string.custom_layout_delete_button) { state.deleteSelectedButtons() }
        }
        if (units.size >= 2) {
            SmallButton(R.string.custom_layout_group) { state.makeGroup() }
        }
        if (units.any { it.startsWith("g") }) {
            SmallButton(R.string.custom_layout_ungroup) { state.ungroup() }
        }
    }
}

// 서로 맞추기: 고른 단위가 둘 이상이면 그것들끼리, 그룹 하나만 골랐으면 그 안의 구성원끼리
@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun RelativeAlignControls(state: CustomLayoutState) {
    var gap by remember { mutableStateOf<Float?>(null) }
    Text(text = stringResource(R.string.custom_layout_section_each))
    FlowRow(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
        SmallButton(R.string.custom_layout_align_left) { state.alignEach(CustomLayoutState.Align.LEFT) }
        SmallButton(R.string.custom_layout_align_hcenter) { state.alignEach(CustomLayoutState.Align.H_CENTER) }
        SmallButton(R.string.custom_layout_align_right) { state.alignEach(CustomLayoutState.Align.RIGHT) }
        SmallButton(R.string.custom_layout_align_top) { state.alignEach(CustomLayoutState.Align.TOP) }
        SmallButton(R.string.custom_layout_align_vcenter) { state.alignEach(CustomLayoutState.Align.V_CENTER) }
        SmallButton(R.string.custom_layout_align_bottom) { state.alignEach(CustomLayoutState.Align.BOTTOM) }
        SmallButton(R.string.custom_layout_dist_h) { state.distribute(true, gap) }
        SmallButton(R.string.custom_layout_dist_v) { state.distribute(false, gap) }
    }
    // 빈칸이면 양 끝 사이를 같은 간격으로
    var gapText by remember { mutableStateOf("") }
    OutlinedTextField(
        value = gapText,
        onValueChange = {
            gapText = it
            gap = it.toFloatOrNull()?.coerceAtLeast(0f)
        },
        label = { Text(stringResource(R.string.custom_layout_gap)) },
        singleLine = true,
        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal),
    )
}

// 기능 이름 (편집기 목록과 화면 버튼 글자에 쓴다)
@Composable
fun customFunctionName(function: CustomButtonFunction): String =
    stringResource(
        when (function) {
            CustomButtonFunction.NONE -> R.string.custom_func_none
            CustomButtonFunction.MENU -> R.string.custom_func_menu
            CustomButtonFunction.FAST_FORWARD -> R.string.custom_func_fast_forward
            CustomButtonFunction.QUICK_SAVE -> R.string.custom_func_quick_save
            CustomButtonFunction.QUICK_LOAD -> R.string.custom_func_quick_load
            CustomButtonFunction.BGM_1X -> R.string.custom_func_bgm_1x
            CustomButtonFunction.SFX_1X -> R.string.custom_func_sfx_1x
            CustomButtonFunction.FF_CUTSCENES -> R.string.custom_func_ff_cutscenes
        },
    )

// 기본 버튼 이름 (키 번호는 Android KeyEvent 값)
@Composable
private fun baseButtonName(key: String): String =
    when {
        key == CustomLayoutKeys.FAST_FORWARD -> stringResource(R.string.game_menu_fast_forward)
        key.startsWith("dir_") -> stringResource(R.string.custom_layout_base_dpad)
        key.startsWith("analog_") -> stringResource(R.string.custom_layout_base_analog)
        key.startsWith("face_") -> stringResource(R.string.custom_layout_base_face)
        key == "key_102" -> "L"
        key == "key_103" -> "R"
        key == "key_104" -> "L2"
        key == "key_105" -> "R2"
        key == "key_108" -> "START"
        key == "key_109" -> "SELECT"
        key == "key_110" -> stringResource(R.string.custom_func_menu)
        else -> key
    }

// 배치 전체: 여백, 격자, 비율 유지, 빨리 감기 버튼, 좌우 대칭
@Composable
private fun LayoutControls(state: CustomLayoutState) {
    Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
        NumberField(
            modifier = Modifier.weight(1f),
            label = stringResource(R.string.custom_layout_margin),
            fieldKey = "margin",
            value = state.layout.margin,
            decimals = false,
            onValue = { state.layout = state.layout.copy(margin = it.coerceAtLeast(0f)) },
        )
        NumberField(
            modifier = Modifier.weight(1f),
            label = stringResource(R.string.custom_layout_grid),
            fieldKey = "grid",
            value = state.layout.grid,
            decimals = false,
            onValue = { state.layout = state.layout.copy(grid = it.coerceAtLeast(0f)) },
        )
    }
    SmallButton(R.string.custom_layout_mirror) { state.mirrorLeftToRight() }
    Row(verticalAlignment = Alignment.CenterVertically) {
        Text(
            modifier = Modifier.weight(1f),
            text = stringResource(R.string.custom_layout_keep_aspect),
        )
        Switch(
            checked = state.layout.keepAspect,
            onCheckedChange = { state.setKeepAspect(it) },
        )
    }
    Row(verticalAlignment = Alignment.CenterVertically) {
        Text(
            modifier = Modifier.weight(1f),
            text = stringResource(R.string.custom_layout_show_fast_forward),
        )
        Switch(
            checked = state.layout.showFastForwardButton,
            onCheckedChange = { state.layout = state.layout.copy(showFastForwardButton = it) },
        )
    }
}

@Composable
private fun SmallButton(
    textRes: Int,
    onClick: () -> Unit,
) {
    OutlinedButton(onClick = onClick) {
        Text(stringResource(textRes))
    }
}

// 숫자 입력 칸. 입력 중이 아닐 때는 끌기 등으로 바뀐 값을 따라 표시한다 (소수 첫째 자리 또는 정수)
@Composable
private fun NumberField(
    modifier: Modifier,
    label: String,
    fieldKey: String,
    value: Float,
    decimals: Boolean = true,
    onValue: (Float) -> Unit,
) {
    fun format(v: Float) = if (decimals) ((v * 10f).roundToInt() / 10f).toString() else v.roundToInt().toString()

    var text by remember(fieldKey) { mutableStateOf(format(value)) }
    var focused by remember(fieldKey) { mutableStateOf(false) }
    LaunchedEffect(fieldKey, value, focused) {
        if (!focused) text = format(value)
    }
    OutlinedTextField(
        modifier = modifier.onFocusChanged { focused = it.isFocused },
        value = text,
        onValueChange = { input ->
            text = input
            input.replace(',', '.').toFloatOrNull()?.let(onValue)
        },
        label = { Text(label) },
        singleLine = true,
        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal),
    )
}
