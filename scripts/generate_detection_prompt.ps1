param(
    [Parameter(Mandatory=$true)][string]$OutputPath
)
# Run in Windows PowerShell/PowerShell 7; import the WAV into WSL after generation.
Add-Type -AssemblyName System.Speech
$voice = [System.Speech.Synthesis.SpeechSynthesizer]::new()
try {
    $voice.SelectVoice('Microsoft Kangkang')
    $voice.Rate = -1
    $voice.Volume = 100
    $format = [System.Speech.AudioFormat.SpeechAudioFormatInfo]::new(
        16000,
        [System.Speech.AudioFormat.AudioBitsPerSample]::Sixteen,
        [System.Speech.AudioFormat.AudioChannel]::Mono
    )
    $voice.SetOutputToWaveFile($OutputPath, $format)
    $voice.Speak('开始检测')
    $voice.SetOutputToNull()
} finally {
    $voice.Dispose()
}
