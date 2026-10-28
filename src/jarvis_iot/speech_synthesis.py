from elevenlabs.client import ElevenLabs
import os

elevenlabs_client = ElevenLabs(
    api_key=os.getenv("ELEVENLABS_API_KEY")
)

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

    print("[ELEVENLABS] Audio generated and saved locally")
