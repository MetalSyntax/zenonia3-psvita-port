import os
import glob
from deep_translator import GoogleTranslator
import time

def translate_large_text(text):
    paragraphs = text.split('\n\n')
    translated_paragraphs = []
    
    chunk = []
    chunk_len = 0
    
    for p in paragraphs:
        if len(p) + chunk_len > 3000:
            if chunk:
                retries = 3
                while retries > 0:
                    try:
                        translated = GoogleTranslator(source='es', target='en').translate('\n\n'.join(chunk))
                        translated_paragraphs.append(translated)
                        break
                    except Exception as e:
                        retries -= 1
                        print(f"Error translating chunk: {e}. Retrying...")
                        time.sleep(2)
                if retries == 0:
                    translated_paragraphs.append('\n\n'.join(chunk))
                chunk = []
                chunk_len = 0
                time.sleep(0.5)
        chunk.append(p)
        chunk_len += len(p)
        
    if chunk:
        retries = 3
        while retries > 0:
            try:
                translated = GoogleTranslator(source='es', target='en').translate('\n\n'.join(chunk))
                translated_paragraphs.append(translated)
                break
            except Exception as e:
                retries -= 1
                time.sleep(2)
        if retries == 0:
            translated_paragraphs.append('\n\n'.join(chunk))
            
    return '\n\n'.join(translated_paragraphs)

source_dir = 'Docs'
dest_dir = 'Docs/en'

os.makedirs(dest_dir, exist_ok=True)

for file_path in glob.glob(os.path.join(source_dir, '*.md')):
    if os.path.basename(file_path).startswith("._"): continue
    file_name = os.path.basename(file_path)
    dest_path = os.path.join(dest_dir, file_name)
    
    print(f"Translating {file_name}...")
    with open(file_path, 'r', encoding='utf-8') as f:
        content = f.read()
        
    translated_content = translate_large_text(content)
    
    with open(dest_path, 'w', encoding='utf-8') as f:
        f.write(translated_content)
        
    print(f"Saved to {dest_path}")
