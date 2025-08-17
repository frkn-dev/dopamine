import android.Manifest.permission.INTERNET
import android.content.pm.ApplicationInfo
import android.content.pm.PackageInfo
import android.content.pm.PackageManager
import android.content.pm.PackageManager.NameNotFoundException
import android.graphics.Bitmap
import android.graphics.Bitmap.Config.ARGB_8888
import androidx.core.graphics.drawable.toBitmapOrNull
import org.amnezia.vpn.util.Log
import org.json.JSONArray
import org.json.JSONObject

private const val TAG = "AppListProvider"

object AppListProvider {
    fun getAppList(pm: PackageManager, selfPackageName: String): String {
        val jsonArray = JSONArray()
        pm.getPackagesHoldingPermissions(arrayOf(INTERNET), 0)
            .filter { it.packageName != selfPackageName }
            .map { App(it, pm) }
            .sortedWith(App::compareTo)
            .map(App::toJson)
            .forEach(jsonArray::put)
        return jsonArray.toString()
    }

    fun getAppIcon(pm: PackageManager, packageName: String, width: Int, height: Int): Bitmap {
        val icon = try {
            pm.getApplicationIcon(packageName)
        } catch (e: NameNotFoundException) {
            Log.e(TAG, "Package $packageName was not found: $e")
            pm.defaultActivityIcon
        }
        val w: Int = if (width > 0) width else icon.intrinsicWidth
        val h: Int = if (height > 0) height else icon.intrinsicHeight
        return icon.toBitmapOrNull(w, h, ARGB_8888)
            ?: Bitmap.createBitmap(w, h, ARGB_8888)
    }
}

private class App(
    pi: PackageInfo,
    pm: PackageManager,
    ai: ApplicationInfo? = pi.applicationInfo
) : Comparable<App> {

    val packageName: String = pi.packageName

    private val appInfo: ApplicationInfo? = ai ?: run {
        try {
            if (android.os.Build.VERSION.SDK_INT >= 33) {
                pm.getApplicationInfo(packageName, PackageManager.ApplicationInfoFlags.of(0))
            } else {
                @Suppress("DEPRECATION")
                pm.getApplicationInfo(packageName, 0)
            }
        } catch (_: PackageManager.NameNotFoundException) {
            null
        }
    }

    val icon: Boolean = (appInfo?.icon ?: 0) != 0
    val isLaunchable: Boolean = pm.getLaunchIntentForPackage(packageName) != null

    val name: String? = appInfo
        ?.loadLabel(pm)
        ?.toString()
        ?.takeIf { it != packageName }

    override fun compareTo(other: App): Int {
        val r = other.isLaunchable.compareTo(isLaunchable)
        if (r != 0) return r
        if (name != other.name) {
            return when {
                name == null -> 1
                other.name == null -> -1
                else -> String.CASE_INSENSITIVE_ORDER.compare(name, other.name)
            }
        }
        return String.CASE_INSENSITIVE_ORDER.compare(packageName, other.packageName)
    }

    fun toJson(): JSONObject {
        val jsonObject = JSONObject()
        jsonObject.put("package", packageName)
        jsonObject.put("name", name)
        jsonObject.put("icon", icon)
        jsonObject.put("launchable", isLaunchable)
        return jsonObject
    }
}