import os
import re

def translate_glsl_to_cg(glsl_code):
    cg_code = glsl_code
    
    # Remove GLES specific macros and precisions
    cg_code = re.sub(r'#define\s+GLITCH_OPENGLES_2\s*', '', cg_code)
    cg_code = re.sub(r'\bhighp\b', '', cg_code)
    cg_code = re.sub(r'\bmediump\b', '', cg_code)
    cg_code = re.sub(r'\blowp\b', '', cg_code)
    
    # NOTE: this only strips Android/GLES junk (precision qualifiers, the
    # GLITCH_OPENGLES_2 macro). It does NOT restructure attribute/varying
    # into Cg's semantic-tagged parameter list, rename gl_FragColor to a
    # `float4 main() : COLOR` return, or move uniforms outside main() --
    # vitaGL's runtime GLSL->CG translator isn't reliable enough for this
    # game's shaders (see PORTING_PLAN.md), so each new shader hash still
    # needs a real, hand-written .cg file in assets/cg/ following the
    # existing 3 files as a template. Use this script only as a starting
    # point for the boilerplate cleanup, not as a finished translation.

    # Fix precision modifiers that might be left over (e.g. "precision mediump float;")
    cg_code = re.sub(r'precision\s+\w+\s+\w+\s*;', '', cg_code)
    
    return cg_code.strip()

def main():
    dump_dir = "./glsl_dump"
    out_dir = "./assets/cg"
    
    if not os.path.exists(dump_dir):
        print("No glsl_dump folder found!")
        return
        
    if not os.path.exists(out_dir):
        os.makedirs(out_dir)
        
    for fname in os.listdir(dump_dir):
        if fname.endswith(".glsl") and not fname.startswith("._"):
            with open(os.path.join(dump_dir, fname), "r") as f:
                glsl = f.read()
            
            cg = translate_glsl_to_cg(glsl)
            
            out_name = fname.replace(".glsl", ".cg")
            with open(os.path.join(out_dir, out_name), "w") as f:
                f.write(cg)
                
            print(f"Translated {fname} -> {out_name}")

if __name__ == '__main__':
    main()
