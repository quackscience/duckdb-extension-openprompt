<img src="https://github.com/user-attachments/assets/46a5c546-7e9b-42c7-87f4-bc8defe674e0" width=250 />

# DuckDB Open Prompt Extension
Simple extension to query OpenAI Completion API endpoints such as Ollama

> Experimental: USE AT YOUR OWN RISK!

### Functions
- `open_prompt(prompt, model)`
- `set_api_token(auth_token)`
- `set_api_url(completions_url)`
- `set_model_name(model_name)`

### Settings
Setup the completions API configuration w/ optional auth token and model name
```sql
SET VARIABLE openprompt_api_url = 'http://localhost:11434/v1/chat/completions';
SET VARIABLE openprompt_api_token = 'your_api_key_here';
SET VARIABLE openprompt_model_name = 'qwen2.5:0.5b';

```

### Usage
```sql
D SELECT open_prompt('Write a one-line poem about ducks') AS response;
┌────────────────────────────────────────────────┐
│                    response                    │
│                    varchar                     │
├────────────────────────────────────────────────┤
│ Ducks quacking at dawn, swimming in the light. │
└────────────────────────────────────────────────┘
```

#### JSON Structured Output
For supported models you can request structured JSON output by providing a schema. 

```sql
SET VARIABLE openprompt_api_url = 'http://localhost:11434/v1/chat/completions';
SET VARIABLE openprompt_api_token = 'your_api_key_here';
SET VARIABLE openprompt_model_name = 'llama3.2:3b';

SELECT open_prompt('I want ice cream', json_schema := '{
       "type": "object",
       "properties": {
         "summary": { "type": "string" },
         "sentiment": { "type": "string", "enum": ["pos", "neg", "neutral"] }
       },
       "required": ["summary", "sentiment"],
       "additionalProperties": false
     }');
```

For smaller models, the `system_prompt` can be used to request JSON in _best-effort_ mode

```sql
SELECT open_prompt('I want ice cream', system_prompt := '{
       "type": "object",
       "properties": {
            "summary": { "type": "string" },
            "favourite_animals": { "type": "array" },
            "favourite_activity": { "type": "aray" },
            "star_rating": { "type": "number" }
       },
       "struct_descr": {"star_rating": "rating on a scale from 1 (bad) to 5 (best)"}
');

D SELECT open_prompt('My zoo visit was fun and I loved the bears and tigers. i also had icecream') AS response;
┌─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┐
│ response │
│ varchar │
├─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┤
│ {"summary": "A short summary of your recent zoo visit activity.", "favourite_animals": ["bears", "tigers"], "favourite_activity": ["icecream"], "star … │
└─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┘

```



<br>

<img src="https://github.com/user-attachments/assets/824bfab2-aca6-4bd9-8a4a-bc01901fcd5b" width=100 />

### Ollama self-hosted
Test the open_prompt extension using a local or remote Ollama with Completions API

#### CPU only
```
docker run -d -v ollama:/root/.ollama -p 11434:11434 --name ollama ollama/ollama
```
#### Nvidia GPU
Install the Nvidia container toolkit. Run Ollama inside a Docker container
```
docker run -d --gpus=all -v ollama:/root/.ollama -p 11434:11434 --name ollama ollama/ollama
```
