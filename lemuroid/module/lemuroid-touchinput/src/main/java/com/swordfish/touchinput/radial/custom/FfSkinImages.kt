package com.swordfish.touchinput.radial.custom

import android.graphics.BitmapFactory
import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.compositionLocalOf
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import java.io.File

/**
 * ff-bgm1x: 스킨(zip, Pizza Boy 형식)의 버튼 그림. 이름: a, b, l, r, x, y, start, select, cross, cross_u ... (뒤에 _pressed)
 * 그림이 없는 버튼은 원래 모양대로 그린다.
 */
class FfSkinImages(private val dir: File) {
    private val cache = HashMap<String, ImageBitmap?>()

    fun get(name: String): ImageBitmap? =
        cache.getOrPut(name) {
            File(dir, "$name.png").takeIf { it.isFile }?.let { BitmapFactory.decodeFile(it.path)?.asImageBitmap() }
        }

    /** 눌림 그림이 있으면 그것, 없으면 기본 그림 */
    fun button(
        name: String,
        pressed: Boolean,
    ): ImageBitmap? = (if (pressed) get(name + "_pressed") else null) ?: get(name)

    /** 십자키: 누른 방향(오른쪽 +x, 아래 +y)의 그림 */
    fun cross(direction: Offset): ImageBitmap? {
        val h = when {
            direction.x > 0.3f -> "r"
            direction.x < -0.3f -> "l"
            else -> ""
        }
        val v = when {
            direction.y > 0.3f -> "d"
            direction.y < -0.3f -> "u"
            else -> ""
        }
        val dir = v + h
        return (if (dir.isNotEmpty()) get("cross_$dir") else null) ?: get("cross")
    }

    companion object {
        /** 버튼 글자·아이콘 이름 -> 스킨 그림 이름 */
        fun nameOf(
            label: String?,
            iconName: String?,
        ): String? =
            when {
                label != null && label.length <= 2 -> label.lowercase()
                iconName == "button_start" -> "start"
                iconName == "button_select" -> "select"
                else -> null
            }
    }
}

val LocalFfSkinImages = compositionLocalOf<FfSkinImages?> { null }

@Composable
fun FfSkinImage(image: ImageBitmap) {
    Image(bitmap = image, contentDescription = null, modifier = Modifier.fillMaxSize(), contentScale = ContentScale.Fit)
}
