from jarvis_iot.jarvis import transcribe_audio, jarvis_agent
from jarvis_iot.llm import get_jarvis_response
from jarvis_iot.speech_synthesis import generate_speech

def invoke_jarvis(command: str):
    """
    Invokes the Jarvis agent to process user input and generate a response.

    Returns:
        dict: The response from the Jarvis agent.
    """
    transcription = transcribe_audio("response.wav")  # Transcribe the audio file (if needed)    
    
    print(f"[WHISTLE] {transcription["text"]}")    

    agent_response = jarvis_agent.run(command)["results"]  # Run the Jarvis agent with the command

    print(f"[NEEDLE] {agent_response}")

    # Get response from Jarvis LLM
    llm_response = get_jarvis_response(str(agent_response))

    # Generate speech from the response
    speech_file_path = generate_speech(llm_response['content'])

    return llm_response