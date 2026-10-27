import needle
from elevenlabs.client import ElevenLabs
import os
from dotenv import load_dotenv

load_dotenv()  # Load environment variables from .env file

elevenlabs_client = ElevenLabs(
    api_key=os.getenv("ELEVENLABS_API_KEY")
)

def transcribe_audio(file_path: str) -> dict:
    """
    Transcribes the audio from the given file path using the Needle library.

    Args:
        file_path (str): The path to the audio file to be transcribed.

    Returns:
        dict: The transcribed text and other metadata.
    """
    return needle.transcribe(file_path)

def generate_speech(text:str):
    """
    Generates speech from the given text using the ElevenLabs API.

    Args:
        text (str): The text to be converted to speech.
    Returns:
        str: The path to the generated audio file.
    """
    audio = elevenlabs_client.text_to_speech.convert(
        text=text,
        voice_id="EXAVITQu4vr4xnSDxMaL",
        model_id="eleven_multilingual_v2",
        output_format="wav_22050",
    )

    with open("response.wav", "wb") as file:
        for chunk in audio:
            file.write(chunk)

    print("Jarvis response saved as response.wav")

generate_speech("Hello, I am Jarvis, your personal AI assistant. How can I help you today?")