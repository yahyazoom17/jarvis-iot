import needle
import datetime

def transcribe_audio(file_path: str) -> dict:
    """
    Transcribes the audio from the given file path using the Needle library.

    Args:
        file_path (str): The path to the audio file to be transcribed.

    Returns:
        dict: The transcribed text and other metadata.
    """
    print("[INFO] Transcribing audio...")
    return needle.transcribe(file_path)

@needle.tool
def get_weather(city: str) -> dict:
    "Get the current weather for a city."
    return {
        "city": city,
        "temp_c": 27,
        "sky": "clear"
    }

@needle.tool
def evaluate_maths(expression: str) -> dict:
    "Evaluate a mathematical expression."
    try:
        result = eval(expression)
        return {"expression": expression, "result": result}
    except Exception as e:
        return {"expression": expression, "error": str(e)}

@needle.tool
def get_time() -> dict:
    "Get the current time."
    return {
        "current_time": datetime.datetime.now().strftime("%I:%M %p"),
        "current_date": datetime.datetime.now().strftime("%d-%m-%Y")
    }

jarvis_agent = needle.Needle(tools=[get_weather, evaluate_maths, get_time])