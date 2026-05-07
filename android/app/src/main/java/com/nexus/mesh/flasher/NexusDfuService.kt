package com.nexus.mesh.flasher

import android.app.Activity
import com.nexus.mesh.ui.MainActivity
import no.nordicsemi.android.dfu.DfuBaseService

/**
 * Concrete DFU service required by Nordic's library — `DfuBaseService` is
 * abstract and the library's own bundled `DfuService` class became
 * package-private in 2.x, so we ship our own minimal subclass.
 *
 * Tap-on-notification routes back to MainActivity so the user can return
 * to the flash flow without re-launching the app.
 */
class NexusDfuService : DfuBaseService() {
    override fun getNotificationTarget(): Class<out Activity> = MainActivity::class.java
    override fun isDebug(): Boolean = false
}
