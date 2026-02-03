package com.dtt.clion

import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.components.PersistentStateComponent
import com.intellij.openapi.components.State
import com.intellij.openapi.components.Storage
import com.intellij.util.xmlb.XmlSerializerUtil

@State(name = "DTTSettings", storages = [Storage("dtt.xml")])
class DTTSettings : PersistentStateComponent<DTTSettings.State> {
    data class State(
        var clangPath: String = "",
        var pluginPath: String = "",
        var standard: String = "c++20",
        var mode: String = "inform",
        var debounceMs: Int = 500,
        var analyzeOnSave: Boolean = true,
        var analyzeOnChange: Boolean = true,
        var autoDownload: Boolean = true
    )

    @Volatile
    private var state = State()

    override fun getState(): State = state

    override fun loadState(state: State) {
        XmlSerializerUtil.copyBean(state, this.state)
    }

    /**
     * Thread-safe update of a single property.
     * This is the recommended way to update settings from background threads.
     */
    @Synchronized
    fun updateClangPath(path: String) {
        state.clangPath = path
    }

    @Synchronized
    fun updatePluginPath(path: String) {
        state.pluginPath = path
    }

    companion object {
        fun getInstance(): DTTSettings =
            ApplicationManager.getApplication().getService(DTTSettings::class.java)
    }
}
