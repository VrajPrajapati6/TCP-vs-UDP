Add-Type -AssemblyName System.Speech
$synth = New-Object System.Speech.Synthesis.SpeechSynthesizer
$format = New-Object System.Speech.AudioFormat.SpeechAudioFormatInfo(22050, [System.Speech.AudioFormat.AudioBitsPerSample]::Sixteen, [System.Speech.AudioFormat.AudioChannel]::Mono)
$synth.SetOutputToWaveFile("d:\Github\TCP-vs-UDP\RealtimeAudioDemo\audio\network_demo.wav", $format)
$synth.Speak("Welcome to the TCP versus UDP real-time communication demonstration. TCP provides reliable and ordered delivery using acknowledgments and retransmissions. UDP does not guarantee delivery, but it can provide lower delay and is commonly used for real-time applications. In this demonstration, listen carefully to how packet loss affects UDP playback, and how reliable delivery through TCP can introduce waiting when data is delayed.")
$synth.Dispose()
Write-Host "Audio generated successfully: d:\Github\TCP-vs-UDP\RealtimeAudioDemo\audio\network_demo.wav"
