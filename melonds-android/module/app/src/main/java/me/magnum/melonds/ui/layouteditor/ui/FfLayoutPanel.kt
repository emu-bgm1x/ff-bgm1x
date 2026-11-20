package me.magnum.melonds.ui.layouteditor.ui

import androidx.compose.foundation.gestures.detectDragGestures
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
import androidx.compose.material.AlertDialog
import androidx.compose.material.Card
import androidx.compose.material.OutlinedButton
import androidx.compose.material.OutlinedTextField
import androidx.compose.material.Text
import androidx.compose.material.TextButton
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
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import me.magnum.melonds.R
import me.magnum.melonds.domain.model.layout.LayoutComponent
import me.magnum.melonds.ui.layouteditor.LayoutEditorView
import me.magnum.melonds.utils.getLayoutComponentName
import kotlin.math.roundToInt

/**
 * ff-bgm1x: melonDS 레이아웃 편집기의 새 편집 패널 (3DS·GBA 편집기와 같은 방식).
 * 화면 위에 떠 있고 끌어서 옮기거나 접을 수 있다. 원래 아래 패널 대신 쓴다.
 * [tick] 은 편집기에서 무언가 바뀔 때마다 늘어나는 값으로, 이 값이 바뀌면 다시 그린다.
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
fun FfLayoutPanel(
    editor: LayoutEditorView,
    tick: Int,
    margin: Int,
    grid: Int,
    onMarginChanged: (Int) -> Unit,
    onGridChanged: (Int) -> Unit,
    onFillScreen: () -> Unit,
    onOpenMenu: () -> Unit,
    onUseOriginalEditor: () -> Unit,
) {
    var panelOffset by remember { mutableStateOf(Offset.Zero) }
    var collapsed by remember { mutableStateOf(false) }
    var gap by remember { mutableStateOf<Int?>(null) }
    // 기능 고르기 창 (열려 있으면 고른 뒤 부를 함수)
    var choosing by remember { mutableStateOf<((LayoutComponent) -> Unit)?>(null) }

    choosing?.let { onChosen ->
        val available = editor.availableComponents()
        AlertDialog(
            onDismissRequest = { choosing = null },
            title = { Text(stringResource(R.string.ff_layout_choose_function)) },
            text = {
                Column(modifier = Modifier.heightIn(max = 360.dp).verticalScroll(rememberScrollState())) {
                    if (available.isEmpty()) Text(stringResource(R.string.no_more_components))
                    available.forEach { component ->
                        TextButton(
                            modifier = Modifier.fillMaxWidth(),
                            onClick = {
                                choosing = null
                                onChosen(component)
                            },
                        ) {
                            Text(modifier = Modifier.fillMaxWidth(), text = stringResource(getLayoutComponentName(component)))
                        }
                    }
                }
            },
            buttons = {
                Row(modifier = Modifier.fillMaxWidth().padding(8.dp), horizontalArrangement = Arrangement.End) {
                    TextButton(onClick = { choosing = null }) { Text(stringResource(R.string.cancel)) }
                }
            },
        )
    }

    Box(modifier = Modifier.fillMaxSize().windowInsetsPadding(WindowInsets.safeDrawing)) {
        Card(
            modifier = Modifier
                .align(Alignment.TopCenter)
                .offset { IntOffset(panelOffset.x.roundToInt(), panelOffset.y.roundToInt()) }
                .padding(8.dp)
                .widthIn(max = 460.dp),
            elevation = 8.dp,
        ) {
            Column(modifier = Modifier.padding(8.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                // 패널을 끌어 옮기는 손잡이와 접기/펴기
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(
                        modifier = Modifier
                            .weight(1f)
                            .pointerInput(Unit) {
                                detectDragGestures { change, amount ->
                                    change.consume()
                                    panelOffset += amount
                                }
                            }
                            .padding(vertical = 8.dp),
                        text = stringResource(R.string.ff_layout_panel_handle),
                    )
                    TextButton(onClick = { collapsed = !collapsed }) {
                        Text(stringResource(if (collapsed) R.string.ff_layout_panel_expand else R.string.ff_layout_panel_collapse))
                    }
                }
                if (!collapsed) {
                    Column(
                        modifier = Modifier.heightIn(max = 300.dp).verticalScroll(rememberScrollState()),
                        verticalArrangement = Arrangement.spacedBy(4.dp),
                    ) {
                        PanelBody(editor, tick, margin, grid, gap, { gap = it }, onMarginChanged, onGridChanged, onFillScreen) { choosing = it }
                    }
                }
                Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.End) {
                    TextButton(onClick = onUseOriginalEditor) { Text(stringResource(R.string.ff_layout_use_original)) }
                    TextButton(onClick = onOpenMenu) { Text(stringResource(R.string.menu)) }
                }
            }
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun PanelBody(
    editor: LayoutEditorView,
    tick: Int,
    margin: Int,
    grid: Int,
    gap: Int?,
    onGap: (Int?) -> Unit,
    onMarginChanged: (Int) -> Unit,
    onGridChanged: (Int) -> Unit,
    onFillScreen: () -> Unit,
    choose: (((LayoutComponent) -> Unit)) -> Unit,
) {
    val view = editor.selectedViewOrNull()
    val count = editor.selectionCount()
    val units = editor.selectedUnits()
    Text(
        text = when {
            view == null -> stringResource(R.string.ff_layout_hint)
            count > 1 -> stringResource(R.string.ff_layout_selected_many, stringResource(getLayoutComponentName(view.component)), count - 1)
            else -> stringResource(getLayoutComponentName(view.component))
        },
    )
    if (view != null) {
        val p = view.getPosition()
        Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
            NumberField(Modifier.weight(1f), stringResource(R.string.ff_layout_x), "x$tick${view.component}", p.x) { editor.moveSelectedViewTo(it, null) }
            NumberField(Modifier.weight(1f), stringResource(R.string.ff_layout_y), "y$tick${view.component}", p.y) { editor.moveSelectedViewTo(null, it) }
            NumberField(Modifier.weight(1f), stringResource(R.string.ff_layout_w), "w$tick${view.component}", view.getWidth()) { w ->
                if (w > 0) {
                    // 버튼은 정사각형, 화면은 높이 그대로
                    val h = if (view.component.isScreen()) view.getHeight() else w
                    editor.resizeSelectedView(w, h)
                }
            }
            if (view.component.isScreen()) {
                NumberField(Modifier.weight(1f), stringResource(R.string.ff_layout_h), "h$tick${view.component}", view.getHeight()) { h ->
                    if (h > 0) editor.resizeSelectedView(view.getWidth(), h)
                }
            }
        }
    }

    // 버튼 만들기·바꾸기·지우기·그룹
    FlowRow(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
        SmallButton(R.string.ff_layout_add_button) { choose { editor.addComponentCentered(it) } }
        if (view != null && count == 1 && !view.component.isScreen()) {
            SmallButton(R.string.ff_layout_change_function) { choose { editor.changeSelectedComponent(it) } }
        }
        if (view != null) SmallButton(R.string.ff_layout_delete_button) { editor.deleteSelection() }
        if (units.size >= 2) SmallButton(R.string.ff_layout_group) { editor.makeGroup() }
        if (editor.hasGroupSelected()) SmallButton(R.string.ff_layout_ungroup) { editor.ungroup() }
        if (count > 1 && view != null && !view.component.isScreen()) {
            SmallButton(R.string.ff_layout_same_size) { editor.sameSizeForSelection() }
        }
    }

    // 서로 맞추기
    if (editor.alignTargets().size >= 2) {
        Text(stringResource(R.string.ff_layout_section_each))
        FlowRow(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
            SmallButton(R.string.ff_layout_align_left) { editor.alignEach(LayoutEditorView.Align.LEFT) }
            SmallButton(R.string.ff_layout_align_hcenter) { editor.alignEach(LayoutEditorView.Align.H_CENTER) }
            SmallButton(R.string.ff_layout_align_right) { editor.alignEach(LayoutEditorView.Align.RIGHT) }
            SmallButton(R.string.ff_layout_align_top) { editor.alignEach(LayoutEditorView.Align.TOP) }
            SmallButton(R.string.ff_layout_align_vcenter) { editor.alignEach(LayoutEditorView.Align.V_CENTER) }
            SmallButton(R.string.ff_layout_align_bottom) { editor.alignEach(LayoutEditorView.Align.BOTTOM) }
            SmallButton(R.string.ff_layout_dist_h) { editor.distribute(true, gap) }
            SmallButton(R.string.ff_layout_dist_v) { editor.distribute(false, gap) }
        }
        var gapText by remember { mutableStateOf(gap?.toString() ?: "") }
        OutlinedTextField(
            value = gapText,
            onValueChange = {
                gapText = it
                onGap(it.toIntOrNull()?.coerceAtLeast(0))
            },
            label = { Text(stringResource(R.string.ff_layout_gap)) },
            singleLine = true,
            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
        )
    }

    // 화면 기준
    Text(stringResource(R.string.ff_layout_section_screen))
    FlowRow(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
        SmallButton(R.string.center_horizontal) { editor.centerSelectedViewHorizontally() }
        SmallButton(R.string.center_vertical) { editor.centerSelectedViewVertically() }
        SmallButton(R.string.ff_layout_edge_left) { editor.alignSelectedView(LayoutEditorView.Edge.LEFT, margin) }
        SmallButton(R.string.ff_layout_edge_right) { editor.alignSelectedView(LayoutEditorView.Edge.RIGHT, margin) }
        SmallButton(R.string.ff_layout_edge_top) { editor.alignSelectedView(LayoutEditorView.Edge.TOP, margin) }
        SmallButton(R.string.ff_layout_edge_bottom) { editor.alignSelectedView(LayoutEditorView.Edge.BOTTOM, margin) }
        if (view != null && view.component.isScreen() && count == 1) SmallButton(R.string.ff_layout_fill) { onFillScreen() }
        SmallButton(R.string.ff_layout_mirror) { editor.mirrorLeftToRight() }
    }

    // 여백·격자
    Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
        NumberField(Modifier.weight(1f), stringResource(R.string.ff_layout_margin_title), "margin", margin) { onMarginChanged(it.coerceAtLeast(0)) }
        NumberField(Modifier.weight(1f), stringResource(R.string.ff_layout_grid_title), "grid", grid) { onGridChanged(it.coerceAtLeast(0)) }
    }
}

@Composable
private fun SmallButton(textRes: Int, onClick: () -> Unit) {
    OutlinedButton(onClick = onClick) { Text(stringResource(textRes)) }
}

// 숫자 입력 칸. 입력 중이 아닐 때는 끌기 등으로 바뀐 값을 따라 표시한다
@Composable
private fun NumberField(modifier: Modifier, label: String, fieldKey: String, value: Int, onValue: (Int) -> Unit) {
    var text by remember { mutableStateOf(value.toString()) }
    var focused by remember { mutableStateOf(false) }
    LaunchedEffect(fieldKey, value, focused) {
        if (!focused) text = value.toString()
    }
    OutlinedTextField(
        modifier = modifier.onFocusChanged { focused = it.isFocused },
        value = text,
        onValueChange = { input ->
            text = input
            input.toIntOrNull()?.let(onValue)
        },
        label = { Text(label) },
        singleLine = true,
        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
    )
}
