package com.swordfish.lemuroid.app.mobile.feature.gamemenu.cheats

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import com.swordfish.lemuroid.R
import com.swordfish.lemuroid.app.shared.cheats.Cheat
import com.swordfish.lemuroid.app.shared.cheats.CheatsManager
import com.swordfish.lemuroid.lib.library.db.entity.Game
import timber.log.Timber

// ff-bgm1x: 게임 메뉴의 치트 화면. 바뀔 때마다 저장하고 onChanged 로 알린다
@Composable
fun GameMenuCheatsScreen(
    game: Game,
    cheatsManager: CheatsManager,
    onChanged: () -> Unit,
) {
    val context = LocalContext.current
    val cheats = remember { mutableStateListOf<Cheat>().apply { addAll(cheatsManager.load(game)) } }
    var showAddDialog by remember { mutableStateOf(false) }

    fun commit() {
        cheatsManager.save(game, cheats.toList())
        onChanged()
    }

    val importLauncher =
        rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
            if (uri == null) return@rememberLauncherForActivityResult
            val text =
                runCatching {
                    context.contentResolver.openInputStream(uri)?.use { it.bufferedReader().readText() }
                }.onFailure { Timber.e(it, "Cannot read cht file") }.getOrNull()
            val imported = text?.let { CheatsManager.parseCht(it) }.orEmpty()
            if (imported.isNotEmpty()) {
                cheats.addAll(imported)
                commit()
            }
        }

    Column(modifier = Modifier.fillMaxSize()) {
        Row(
            modifier = Modifier.fillMaxWidth().padding(16.dp),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            OutlinedButton(onClick = { showAddDialog = true }) {
                Text(stringResource(R.string.game_menu_cheats_add))
            }
            OutlinedButton(onClick = { importLauncher.launch(arrayOf("*/*")) }) {
                Text(stringResource(R.string.game_menu_cheats_import))
            }
        }

        if (cheats.isEmpty()) {
            Text(
                text = stringResource(R.string.game_menu_cheats_empty),
                modifier = Modifier.padding(16.dp),
                style = MaterialTheme.typography.bodyMedium,
            )
        }

        LazyColumn(modifier = Modifier.fillMaxSize()) {
            itemsIndexed(cheats) { index, cheat ->
                Row(
                    modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 4.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Column(modifier = Modifier.weight(1f)) {
                        Text(text = cheat.name, style = MaterialTheme.typography.bodyLarge)
                        Text(text = cheat.code, style = MaterialTheme.typography.bodySmall, maxLines = 2)
                    }
                    Switch(
                        checked = cheat.enabled,
                        onCheckedChange = {
                            cheats[index] = cheat.copy(enabled = it)
                            commit()
                        },
                    )
                    IconButton(
                        onClick = {
                            cheats.removeAt(index)
                            commit()
                        },
                    ) {
                        Icon(Icons.Filled.Delete, stringResource(R.string.game_menu_cheats_delete))
                    }
                }
            }
        }
    }

    if (showAddDialog) {
        var name by remember { mutableStateOf("") }
        var code by remember { mutableStateOf("") }
        AlertDialog(
            onDismissRequest = { showAddDialog = false },
            title = { Text(stringResource(R.string.game_menu_cheats_add)) },
            text = {
                Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    OutlinedTextField(
                        value = name,
                        onValueChange = { name = it },
                        label = { Text(stringResource(R.string.game_menu_cheats_name)) },
                        singleLine = true,
                    )
                    OutlinedTextField(
                        value = code,
                        onValueChange = { code = it },
                        label = { Text(stringResource(R.string.game_menu_cheats_code)) },
                        minLines = 3,
                    )
                }
            },
            confirmButton = {
                TextButton(
                    enabled = code.isNotBlank(),
                    onClick = {
                        val cheatName = name.ifBlank { context.getString(R.string.game_menu_cheats_default_name, cheats.size + 1) }
                        cheats.add(Cheat(cheatName, code.trim(), true))
                        commit()
                        showAddDialog = false
                    },
                ) {
                    Text(stringResource(android.R.string.ok))
                }
            },
            dismissButton = {
                TextButton(onClick = { showAddDialog = false }) {
                    Text(stringResource(android.R.string.cancel))
                }
            },
        )
    }
}
