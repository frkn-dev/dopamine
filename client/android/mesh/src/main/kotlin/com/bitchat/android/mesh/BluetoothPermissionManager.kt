package com.bitchat.android.mesh

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import androidx.core.app.ActivityCompat

/**
 * Handles all Bluetooth permission checking logic
 */
class BluetoothPermissionManager(private val context: Context) {
    
    /**
     * Check if all required Bluetooth permissions are granted.
     *
     * On API 31+ (S) the BLUETOOTH_* runtime permissions are sufficient and
     * location is NOT required: the app manifest declares BLUETOOTH_SCAN with
     * neverForLocation and requests ACCESS_FINE_LOCATION only up to SDK 30.
     * The stock BitChat check demanded location unconditionally, which can
     * never be satisfied here.
     */
    fun hasBluetoothPermissions(): Boolean {
        val permissions = if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.S) {
            listOf(
                Manifest.permission.BLUETOOTH_ADVERTISE,
                Manifest.permission.BLUETOOTH_CONNECT,
                Manifest.permission.BLUETOOTH_SCAN
            )
        } else {
            listOf(
                Manifest.permission.BLUETOOTH,
                Manifest.permission.BLUETOOTH_ADMIN,
                Manifest.permission.ACCESS_COARSE_LOCATION,
                Manifest.permission.ACCESS_FINE_LOCATION
            )
        }

        return permissions.all {
            ActivityCompat.checkSelfPermission(context, it) == PackageManager.PERMISSION_GRANTED
        }
    }
}
