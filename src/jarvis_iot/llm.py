import os
from jarvis_iot.config import JARVIS_SYSTEM_PROMPT
from litellm import completion

messages = [
    {
        "role": "system",
        "content": JARVIS_SYSTEM_PROMPT
    },    
]

def get_jarvis_response(user_input: str) -> dict:
    messages.append({
        "role":"user",
        "content":user_input
    })
    response = completion(
        model="openrouter/nvidia/nemotron-3.5-lightning:free",
        messages=messages,
        api_key=os.environ["OPENROUTER_API_KEY"],
    )
    
    llm_response = response.choices[0].message
    
    messages.append({
        "role":"assistant",
        "content":llm_response.content
    })

    print("[LLM] ", llm_response.content)
    return llm_response