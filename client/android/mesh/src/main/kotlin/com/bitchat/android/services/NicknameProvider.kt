package com.bitchat.android.services

import android.content.Context

object NicknameProvider {
    @Volatile
    private var nickname: String = "anon"

    fun setNickname(value: String) {
        val trimmed = value.trim()
        nickname = if (trimmed.isEmpty()) "anon" else trimmed
    }

    fun getNickname(context: Context, myPeerID: String): String = nickname
}
