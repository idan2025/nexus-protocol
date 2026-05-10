package com.nexus.mesh.updater

import android.app.AlarmManager
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.os.Build
import android.util.Log
import androidx.core.app.NotificationCompat
import com.nexus.mesh.R

/**
 * Persists a user-scheduled update reminder and fires a notification
 * at the chosen time via AlarmManager. The notification taps back into
 * MainActivity with [EXTRA_SHOW_UPDATE] so the dialog reappears with
 * "Update now" front-and-centre.
 */
object UpdateScheduler {
    private const val TAG = "UpdateScheduler"
    private const val ACTION = "com.nexus.mesh.updater.SCHEDULED_UPDATE"
    private const val NOTIF_ID = 0x4E58 // 'NX'
    private const val CHANNEL_ID = "nexus_update"
    private const val PREFS = "nexus_update_schedule"
    private const val KEY_AT = "at_ms"
    private const val KEY_TAG = "tag"

    const val EXTRA_SHOW_UPDATE = "show_update_dialog"

    fun schedule(context: Context, atMs: Long, tag: String) {
        val am = context.getSystemService(Context.ALARM_SERVICE) as AlarmManager
        val pi = pendingIntent(context, tag)
        try {
            am.setAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, atMs, pi)
            prefs(context).edit()
                .putLong(KEY_AT, atMs)
                .putString(KEY_TAG, tag)
                .apply()
            Log.i(TAG, "Scheduled update reminder for $tag at $atMs")
        } catch (e: SecurityException) {
            Log.w(TAG, "AlarmManager denied: $e")
        }
    }

    fun cancel(context: Context) {
        val am = context.getSystemService(Context.ALARM_SERVICE) as AlarmManager
        am.cancel(pendingIntent(context, scheduledTag(context) ?: ""))
        prefs(context).edit().remove(KEY_AT).remove(KEY_TAG).apply()
    }

    fun scheduledAtMs(context: Context): Long = prefs(context).getLong(KEY_AT, 0L)
    fun scheduledTag(context: Context): String? = prefs(context).getString(KEY_TAG, null)

    /** True if a future schedule exists for [tag]. Past schedules are
     *  treated as expired and ignored. */
    fun hasPending(context: Context, tag: String): Boolean {
        if (scheduledTag(context) != tag) return false
        return scheduledAtMs(context) > System.currentTimeMillis()
    }

    private fun pendingIntent(context: Context, tag: String): PendingIntent {
        val intent = Intent(context, UpdateScheduledReceiver::class.java).apply {
            action = ACTION
            putExtra(KEY_TAG, tag)
        }
        return PendingIntent.getBroadcast(
            context, 0, intent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )
    }

    fun showReadyNotification(context: Context, tag: String) {
        ensureChannel(context)
        val mainCls = Class.forName("com.nexus.mesh.ui.MainActivity")
        val open = Intent(context, mainCls).apply {
            flags = Intent.FLAG_ACTIVITY_SINGLE_TOP or Intent.FLAG_ACTIVITY_CLEAR_TOP
            putExtra(EXTRA_SHOW_UPDATE, true)
        }
        val pi = PendingIntent.getActivity(
            context, 1, open,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )
        val notif = NotificationCompat.Builder(context, CHANNEL_ID)
            .setSmallIcon(R.drawable.ic_mesh)
            .setContentTitle("Time to update NEXUS Mesh")
            .setContentText("Tap to install $tag")
            .setContentIntent(pi)
            .setAutoCancel(true)
            .setPriority(NotificationCompat.PRIORITY_DEFAULT)
            .build()
        val nm = context.getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
        nm.notify(NOTIF_ID, notif)
    }

    private fun ensureChannel(context: Context) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) return
        val nm = context.getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
        if (nm.getNotificationChannel(CHANNEL_ID) == null) {
            nm.createNotificationChannel(
                NotificationChannel(
                    CHANNEL_ID,
                    "App updates",
                    NotificationManager.IMPORTANCE_DEFAULT
                )
            )
        }
    }

    private fun prefs(context: Context) =
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
}

/** Receiver that AlarmManager wakes at the user-chosen time. */
class UpdateScheduledReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        val tag = intent.getStringExtra("tag")
            ?: UpdateScheduler.scheduledTag(context)
            ?: return
        UpdateScheduler.showReadyNotification(context, tag)
        // Clear stored schedule -- the reminder fired.
        UpdateScheduler.cancel(context)
    }
}
