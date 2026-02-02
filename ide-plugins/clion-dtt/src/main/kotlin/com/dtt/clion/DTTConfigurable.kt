package com.dtt.clion

import com.intellij.openapi.fileChooser.FileChooserDescriptorFactory
import com.intellij.openapi.options.Configurable
import com.intellij.openapi.ui.TextFieldWithBrowseButton
import com.intellij.ui.components.JBCheckBox
import com.intellij.ui.components.JBTextField
import com.intellij.ui.dsl.builder.*
import javax.swing.JComponent

class DTTConfigurable : Configurable {
    private val settings = DTTSettings.getInstance()

    private val clangPath = TextFieldWithBrowseButton()
    private val pluginPath = TextFieldWithBrowseButton()
    private val standard = JBTextField()
    private val mode = JBTextField()
    private val debounceMs = JBTextField()
    private val analyzeOnSave = JBCheckBox()
    private val analyzeOnChange = JBCheckBox()

    override fun getDisplayName(): String = "DTT"

    override fun createComponent(): JComponent {
        clangPath.addBrowseFolderListener(
            "Select Clang++",
            "Path to clang++ executable",
            null,
            FileChooserDescriptorFactory.createSingleFileDescriptor()
        )
        pluginPath.addBrowseFolderListener(
            "Select DTT Plugin",
            "Path to DTT plugin library",
            null,
            FileChooserDescriptorFactory.createSingleFileDescriptor()
        )

        return panel {
            group("Paths") {
                row("Clang++ path:") { cell(clangPath).columns(COLUMNS_LARGE) }
                row("Plugin path:") { cell(pluginPath).columns(COLUMNS_LARGE) }
            }
            group("Analysis") {
                row("C++ standard:") { cell(standard).columns(COLUMNS_SHORT) }
                row("Mode:") {
                    cell(mode).columns(COLUMNS_SHORT)
                    contextHelp("inform, enforce, or quiet")
                }
                row("Debounce (ms):") { cell(debounceMs).columns(COLUMNS_SHORT) }
            }
            group("Triggers") {
                row { cell(analyzeOnSave); analyzeOnSave.text = "Analyze on save" }
                row { cell(analyzeOnChange); analyzeOnChange.text = "Analyze on change" }
            }
        }
    }

    override fun isModified(): Boolean {
        val state = settings.state
        return clangPath.text != state.clangPath ||
                pluginPath.text != state.pluginPath ||
                standard.text != state.standard ||
                mode.text != state.mode ||
                debounceMs.text != state.debounceMs.toString() ||
                analyzeOnSave.isSelected != state.analyzeOnSave ||
                analyzeOnChange.isSelected != state.analyzeOnChange
    }

    override fun apply() {
        settings.loadState(DTTSettings.State(
            clangPath = clangPath.text,
            pluginPath = pluginPath.text,
            standard = standard.text,
            mode = mode.text,
            debounceMs = debounceMs.text.toIntOrNull() ?: 500,
            analyzeOnSave = analyzeOnSave.isSelected,
            analyzeOnChange = analyzeOnChange.isSelected
        ))
    }

    override fun reset() {
        val state = settings.state
        clangPath.text = state.clangPath
        pluginPath.text = state.pluginPath
        standard.text = state.standard
        mode.text = state.mode
        debounceMs.text = state.debounceMs.toString()
        analyzeOnSave.isSelected = state.analyzeOnSave
        analyzeOnChange.isSelected = state.analyzeOnChange
    }
}
