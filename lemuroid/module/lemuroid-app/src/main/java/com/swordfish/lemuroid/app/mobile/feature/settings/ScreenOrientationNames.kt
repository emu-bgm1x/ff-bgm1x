package com.swordfish.lemuroid.app.mobile.feature.settings

import android.content.pm.ActivityInfo
import androidx.compose.runtime.Composable
import androidx.compose.ui.res.stringResource
import com.swordfish.lemuroid.R

// ff-bgm1x: 화면 방향 설정값("auto"/"landscape"/"portrait")의 이름과 Activity 방향 값
@Composable
fun screenOrientationName(value: String): String =
    when (value) {
        "landscape" -> stringResource(R.string.screen_orientation_landscape)
        "portrait" -> stringResource(R.string.screen_orientation_portrait)
        else -> stringResource(R.string.screen_orientation_auto)
    }

fun screenOrientationToActivityInfo(value: String): Int =
    when (value) {
        "landscape" -> ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
        "portrait" -> ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT
        else -> ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED
    }
