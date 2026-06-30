from flask import Flask, request, jsonify, render_template
import subprocess, os, tempfile, shutil, glob, re

app = Flask(__name__)

BINARY = os.path.abspath(
    os.path.join(os.path.dirname(__file__), "../phase3/build/cfg_extractor")
)

@app.route("/")
def index():
    return render_template("index.html")

@app.route("/optimize", methods=["POST"])
def optimize():
    code = request.json.get("code", "")
    if not code.strip():
        return jsonify({"error": "No code provided"}), 400

    tmpdir = tempfile.mkdtemp(prefix="cfg_dash_")

    try:
        src_file = os.path.join(tmpdir, "input.c")
        with open(src_file, "w") as f:
            f.write(code)

        result = subprocess.run(
            [BINARY, "--", src_file],
            cwd=tmpdir,
            capture_output=True,
            text=False,
            timeout=30
        )

        def decode_output(b):
            text = b.decode("utf-8", errors="replace")
            ansi_escape = re.compile(r'\x1B\[[0-9;]*[mK]')
            return ansi_escape.sub('', text)

        response = {}
        response["terminal"] = decode_output(result.stdout) + decode_output(result.stderr)

        opt_c_files = sorted(glob.glob(os.path.join(tmpdir, "*_optimized.c")))
        if opt_c_files:
            combined = ""
            for f_path in opt_c_files:
                with open(f_path, "r", errors="replace") as f:
                    combined += f.read() + "\n"
            response["optimized_code"] = combined
        else:
            response["optimized_code"] = "// No optimized code generated."

        out_static = os.path.join(os.path.dirname(__file__), "static", "output")
        os.makedirs(out_static, exist_ok=True)
        for old in glob.glob(os.path.join(out_static, "*.png")):
            os.remove(old)

        functions = {}

        for png in glob.glob(os.path.join(tmpdir, "*.png")):
            name = os.path.basename(png)
            dest = os.path.join(out_static, name)
            shutil.copy(png, dest)
            url = f"/static/output/{name}"

            if "_cfg_before" in name:
                func = name.replace("_cfg_before.png", "")
                functions.setdefault(func, {})["before"] = url
            elif "_cfg_after" in name:
                func = name.replace("_cfg_after.png", "")
                functions.setdefault(func, {})["after"] = url
            elif "_opt_summary" in name:
                func = name.replace("_opt_summary.png", "")
                functions.setdefault(func, {})["summary"] = url

        response["functions"] = functions
        return jsonify(response)

    except subprocess.TimeoutExpired:
        return jsonify({"error": "Optimizer timed out (30s limit)"}), 500
    except Exception as e:
        return jsonify({"error": str(e)}), 500
    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)

if __name__ == "__main__":
    app.run(debug=True, port=5000)
