import os
from jarvis_iot.config import JARVIS_SYSTEM_PROMPT, JARVIS_USER_PROMPT
from litellm import completion

messages = [
    {
        "role": "system",
        "content": JARVIS_SYSTEM_PROMPT
    },    
]

def get_jarvis_response(user_input: str, tool_results: str) -> dict:
    messages.append({
        "role":"user",
        "content":JARVIS_USER_PROMPT.format(user_query=user_input, tool_results=tool_results)
    })
    # response = completion(
    #     model="openrouter/nvidia/nemotron-3.5-lightning:free",
    #     messages=messages,
    #     api_key=os.environ["OPENROUTER_API_KEY"],
    #     max_completion_tokens=20,
    #     reasoning_effort="none",
    #     max_tokens=1024
    # )
    
    response = completion(
        model="ollama/llama3.2:1b",
        api_base="http://localhost:11434",
        messages=messages,
        max_completion_tokens=20,
        reasoning_effort="none",
        max_tokens=1024
    )    
    
    llm_response = response.choices[0].message
    
    messages.append({
        "role":"assistant",
        "content":llm_response.content
    })

    print("[LLM] ", llm_response.content)
    return llm_response