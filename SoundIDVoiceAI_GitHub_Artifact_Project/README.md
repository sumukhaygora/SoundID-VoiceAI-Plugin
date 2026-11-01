# SoundID VoiceAI — VST3 / AU / Standalone Workstation

A cross-platform C++20 / JUCE 8 AI Voice & Instrument Transformation plugin (`VST3`, `AU`) and `Standalone` application.

## Automatic GitHub Actions Artifact Build
1. Create a new repository on GitHub.
2. Extract and push all files in this folder (including the `.github/workflows/build.yml` folder) to your GitHub repository.
3. Open the **Actions** tab in your GitHub repository.
4. GitHub Actions will automatically fetch JUCE 8, compile the C++20 source code on both **Windows** and **macOS**, and publish ready-to-download `.vst3`, `.component` (AU), and `Standalone` executables under the workflow run's **Artifacts** section.
