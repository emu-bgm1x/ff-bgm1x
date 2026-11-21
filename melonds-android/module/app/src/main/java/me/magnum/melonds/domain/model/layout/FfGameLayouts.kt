package me.magnum.melonds.domain.model.layout

/**
 * ff-bgm1x: 게임 전용 배치 (레무로이드처럼 편집기에서 "이 게임" 으로 저장한 배치).
 * 원래의 배치 목록에 이름 앞머리로 표시해 두고, 배치 목록·고르기 화면에서는 숨긴다.
 * 롬 설정의 layoutId 가 이 배치를 가리킨다.
 */
object FfGameLayouts {
    const val NAME_PREFIX = "ff-bgm1x-game:"

    fun isGameLayout(layout: LayoutConfiguration?): Boolean = layout?.name?.startsWith(NAME_PREFIX) == true

    fun nameFor(gameName: String): String = NAME_PREFIX + gameName
}
