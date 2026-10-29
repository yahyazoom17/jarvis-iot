from pathlib import Path
from jarvis_iot.jarvis import transcribe_audio, jarvis_agent
from jarvis_iot.llm import get_jarvis_response
from jarvis_iot.speech_synthesis import generate_speech
import wave

def save_mic_audio_to_file(audio_buffer: bytes, filename: str) -> Path:
    with wave.open(filename, "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(16000)
        wav.writeframes(audio_buffer)
    return Path(filename)

async def invoke_jarvis():
    """
    Invokes the Jarvis agent to process user input and generate a response.

    Returns:
        dict: The response from the Jarvis agent.
    """
    transcription = transcribe_audio("temp/received_audio.wav")  # Transcribe the audio file (if needed)     
    print(f"[WHISTLE] {transcription['text']}")    

    print(f"[INFO] Invoking needle agent")    
    agent_response = jarvis_agent.run(transcription['text'].lower())["results"]  # Run the Jarvis agent with the command   
    print(f"[NEEDLE] {agent_response}")
    
    # Get response from Jarvis LLM
    print(f"[INFO] Getting response from LLM")
    llm_response = get_jarvis_response(transcription['text'], str(agent_response))

    # Generate speech from the response
    print(f"[INFO] Generating speech from Elevenlabs")
    speech_file_path = generate_speech(llm_response['content'])

    return llm_response['content']