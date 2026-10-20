// https://www.shadertoy.com/view/MsSGWK — iChannel2 is background.jpg
const canvas = document.getElementById("bg");
const gl = canvas.getContext("webgl2", { alpha: false, antialias: false, powerPreference: "high-performance" });
if (!gl) {
  document.documentElement.style.background = "#141416";
} else {
  const vert = `#version 300 es
void main() {
  vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}`;
  const frag = `#version 300 es
precision highp float;
uniform vec3 iResolution;
uniform float iTime;
uniform sampler2D iChannel2;
out vec4 fragColor;

float water(vec3 p) {
  float t = iTime / 4.0;
  p.z += t * 2.0; p.x += t * 2.0;
  vec3 c1 = texture(iChannel2, p.xz / 30.0).xyz;
  p.z += t * 3.0; p.x += t * 0.5;
  vec3 c2 = texture(iChannel2, p.xz / 30.0).xyz;
  p.z += t * 4.0; p.x += t * 0.8;
  vec3 c3 = texture(iChannel2, p.xz / 30.0).xyz;
  c1 += c2 - c3;
  float z = (c1.x + c1.y + c1.z) / 3.0;
  return p.y + z / 4.0;
}

float map(vec3 p) { return water(p); }

float intersect(vec3 ro, vec3 rd) {
  float d = 0.0;
  for (int i = 0; i <= 100; i++) {
    float h = map(ro + rd * d);
    if (h < 0.1) return d;
    d += h;
  }
  return 0.0;
}

vec3 norm(vec3 p) {
  float eps = 0.1;
  return normalize(vec3(
    map(p + vec3(eps, 0.0, 0.0)) - map(p + vec3(-eps, 0.0, 0.0)),
    map(p + vec3(0.0, eps, 0.0)) - map(p + vec3(0.0, -eps, 0.0)),
    map(p + vec3(0.0, 0.0, eps)) - map(p + vec3(0.0, 0.0, -eps))
  ));
}

void mainImage(out vec4 color, in vec2 fragCoord) {
  vec2 uv = fragCoord.xy / iResolution.xy - 0.5;
  uv.x *= iResolution.x / iResolution.y;
  vec3 l1 = normalize(vec3(1.0, 1.0, 1.0));
  vec3 ro = vec3(-3.0, 7.0, -5.0);
  vec3 rc = vec3(0.0);
  vec3 ww = normalize(rc - ro);
  vec3 uu = normalize(cross(vec3(0.0, 1.0, 0.0), ww));
  vec3 vv = normalize(cross(rc - ro, uu));
  vec3 rd = normalize(uu * uv.x + vv * uv.y + ww);
  float d = intersect(ro, rd);
  vec3 c = vec3(0.0);
  if (d > 0.0) {
    vec3 p = ro + rd * d;
    vec3 n = norm(p);
    float spc = pow(max(0.0, dot(reflect(l1, n), rd)), 30.0);
    vec3 rfa = texture(iChannel2, (p + n).xz / 6.0).xyz * (8.0 / d);
    c = rfa + (spc * 0.7) + 0.1;
  }
  color = vec4(c, 1.0);
}

void main() {
  vec4 color;
  mainImage(color, gl_FragCoord.xy);
  fragColor = color;
}`;

  function compile(type, source) {
    const shader = gl.createShader(type);
    gl.shaderSource(shader, source);
    gl.compileShader(shader);
    if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
      console.error(gl.getShaderInfoLog(shader));
      return null;
    }
    return shader;
  }

  const program = gl.createProgram();
  gl.attachShader(program, compile(gl.VERTEX_SHADER, vert));
  gl.attachShader(program, compile(gl.FRAGMENT_SHADER, frag));
  gl.linkProgram(program);
  if (!gl.getProgramParameter(program, gl.LINK_STATUS)) {
    console.error(gl.getProgramInfoLog(program));
  } else {
    const vao = gl.createVertexArray();
    gl.bindVertexArray(vao);
    gl.useProgram(program);
    const resolution = gl.getUniformLocation(program, "iResolution");
    const time = gl.getUniformLocation(program, "iTime");
    const channel = gl.getUniformLocation(program, "iChannel2");
    gl.uniform1i(channel, 0);

    const texture = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, texture);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 1, 1, 0, gl.RGBA, gl.UNSIGNED_BYTE, new Uint8Array([20, 20, 22, 255]));
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.REPEAT);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.REPEAT);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);

    const image = new Image();
    image.onload = () => {
      gl.bindTexture(gl.TEXTURE_2D, texture);
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, image);
      gl.generateMipmap(gl.TEXTURE_2D);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR_MIPMAP_LINEAR);
    };
    image.src = "bg/1.jpg";

    const start = performance.now();
    let running = true;

    function resize() {
      const dpr = Math.min(window.devicePixelRatio || 1, window.innerWidth < 800 ? 1 : 1.25);
      const width = Math.max(1, Math.round(Math.min(window.innerWidth * dpr, 1280)));
      const height = Math.max(1, Math.round(width * window.innerHeight / window.innerWidth));
      if (canvas.width !== width || canvas.height !== height) {
        canvas.width = width;
        canvas.height = height;
      }
    }

    function frame() {
      if (!running) return;
      resize();
      gl.viewport(0, 0, canvas.width, canvas.height);
      gl.uniform3f(resolution, canvas.width, canvas.height, 1);
      gl.uniform1f(time, (performance.now() - start) / 1000);
      gl.drawArrays(gl.TRIANGLES, 0, 3);
      requestAnimationFrame(frame);
    }

    document.addEventListener("visibilitychange", () => {
      const hidden = document.hidden;
      if (hidden) running = false;
      else if (!running) {
        running = true;
        requestAnimationFrame(frame);
      }
    });
    window.addEventListener("resize", resize);
    resize();
    requestAnimationFrame(frame);
  }
}
