import needle
import datetime

@needle.tool
def get_weather(city: str) -> dict:
    "Get the current weather for a city."
    return {
        "city": city,
        "temp_c": 27,
        "sky": "clear"
    }

@needle.tool
def get_time() -> dict:
    "Get the current time."
    return {
        "current_time": datetime.datetime.now().strftime("%I:%M %p"),
        "current_date": datetime.datetime.now().strftime("%d-%m-%Y")
    }

agent = needle.Needle(tools=[get_weather, get_time])