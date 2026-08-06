// `?embed=1` -- output only, for use as a video source (OBS browser source,
// WebLinked). Set before anything renders so the canvas measures the full
// viewport on its first sizing pass rather than the boxed layout's.
const embedParam = new URLSearchParams(location.search).get('embed');
if (embedParam !== null && embedParam !== '0') document.body.dataset.embed = '';
/*
    The outrun web demo: the real stroke pipeline running in WebGL2.

    Engine A traces the built-in demo clip (or your webcam, with permission);
    Engine B generates the paths. Audio comes from a built-in synth loop or
    the microphone, smoothed with the plugin's own asymmetric attack/release.

    The pass chain mirrors the plugin: copy -> edge -> stabilise (ping-pong)
    -> stroke -> glow (three widening separable pairs at quarter size) ->
    composite. Buffers are RGBA16F, which needs EXT_color_buffer_float; the
    page says so plainly if the GPU refuses.
*/

import { VERTEX, COPY, EDGE, STABILISE, STROKE, BLUR, COMPOSITE } from './shaders.js';

const canvas = document.getElementById('view');
const gl = canvas.getContext('webgl2', { antialias: false, alpha: false });
const fail = (msg) => {
	document.getElementById('fail').textContent = msg;
	document.getElementById('fail').style.display = 'block';
	throw new Error(msg);
};
if (!gl) fail('This demo needs WebGL2, which this browser refused to give it.');
if (!gl.getExtension('EXT_color_buffer_float'))
	fail('This demo needs EXT_color_buffer_float (render-to-float), which this GPU refused.');

// --------------------------------------------------------------------------
// Parameters, in the plugin's own 0..1-and-options space.
// --------------------------------------------------------------------------
const state = {
	engine: 0,
	source: 0,          // 0 demo clip, 1 webcam
	detectOn: 3, sensitivity: 0.6, softness: 0.35, detail: 0.15, stability: 0.35,
	trace: 1, traceAngle: 0.0,
	path: 0, pathScale: 0.5, pathDetail: 0.4, horizon: 0.5,
	width: 0.35, core: 0.5,
	breakMode: 0, breakAmount: 0.0, breakSpread: 0.4, breakHue: 0.15,
	colourMode: 0, palette: 2, spread: 0.4,
	colour1: [1.0, 0.2, 0.8], colour2: [0.1, 0.9, 1.0],
	saturation: 0.667, brightness: 0.5,
	speed: 0.25,
	audioLevel: 0.0, audioBreak: 0.0, audioOn: true, mic: false,
	glow: 0.55, glowSize: 0.4, background: 0,
};

// The plugin's Controls.cpp mappings, ported.
const lerp = (a, b, t) => a + (b - a) * Math.min(Math.max(t, 0), 1);
const geometric = (a, b, t) => a * Math.pow(b / a, Math.min(Math.max(t, 0), 1));
const map = {
	sensitivity: v => geometric(0.01, 1.0, v),
	softness: v => lerp(0.05, 1.0, v),
	detail: v => lerp(0.0, 4.0, v),
	attack: v => lerp(1.0, 0.75, v),
	release: v => geometric(1.0, 0.02, v),
	width: v => geometric(1.0, 32.0, v),
	pathScale: v => lerp(0.2, 1.5, v),
	pathDetail: v => geometric(1.0, 16.0, v),
	horizon: v => lerp(0.2, 0.8, v),
	breakHue: v => lerp(0.0, 0.5, v),
	speed: v => (v <= 0.02 ? 0.0 : geometric(0.02, 2.0, (v - 0.02) / 0.98)),
	spread: v => geometric(0.25, 8.0, v),
	saturation: v => lerp(0.0, 1.5, v),
	brightness: v => lerp(0.0, 2.0, v),
	glow: v => lerp(0.0, 3.0, v),
	glowSize: v => geometric(0.5, 8.0, v),
};

// --------------------------------------------------------------------------
// GL plumbing.
// --------------------------------------------------------------------------
function compile(fragSource, name) {
	const make = (type, src) => {
		const s = gl.createShader(type);
		gl.shaderSource(s, src);
		gl.compileShader(s);
		if (!gl.getShaderParameter(s, gl.COMPILE_STATUS))
			fail(`the ${name} shader would not compile: ${gl.getShaderInfoLog(s)}`);
		return s;
	};
	const p = gl.createProgram();
	gl.attachShader(p, make(gl.VERTEX_SHADER, VERTEX));
	gl.attachShader(p, make(gl.FRAGMENT_SHADER, fragSource));
	gl.linkProgram(p);
	if (!gl.getProgramParameter(p, gl.LINK_STATUS))
		fail(`the ${name} shader would not link: ${gl.getProgramInfoLog(p)}`);
	return p;
}

const programs = {
	copy: compile(COPY, 'copy'),
	edge: compile(EDGE, 'edge'),
	stabilise: compile(STABILISE, 'stabilise'),
	stroke: compile(STROKE, 'stroke'),
	blur: compile(BLUR, 'blur'),
	composite: compile(COMPOSITE, 'composite'),
};

function makeTarget(w, h, mipmapped) {
	const tex = gl.createTexture();
	gl.bindTexture(gl.TEXTURE_2D, tex);
	gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA16F, w, h, 0, gl.RGBA, gl.HALF_FLOAT, null);
	gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER,
		mipmapped ? gl.LINEAR_MIPMAP_LINEAR : gl.LINEAR);
	gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
	gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
	gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
	const fbo = gl.createFramebuffer();
	gl.bindFramebuffer(gl.FRAMEBUFFER, fbo);
	gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, tex, 0);
	return { tex, fbo, w, h, mipmapped };
}

let targets = null;
function ensureTargets(w, h) {
	if (targets && targets.w === w && targets.h === h) return;
	targets = {
		w, h,
		copy: makeTarget(w, h, true),
		edge: makeTarget(w, h, false),
		stable: [makeTarget(w, h, true), makeTarget(w, h, true)],
		stroke: makeTarget(w, h, true),
		glow: [
			makeTarget(Math.max(16, w >> 2), Math.max(16, h >> 2), false),
			makeTarget(Math.max(16, w >> 2), Math.max(16, h >> 2), false),
		],
	};
	historyValid = false;
}

// One fullscreen triangle strip quad.
const vao = gl.createVertexArray();
gl.bindVertexArray(vao);
const vbo = gl.createBuffer();
gl.bindBuffer(gl.ARRAY_BUFFER, vbo);
gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 3, -1, -1, 3]), gl.STATIC_DRAW);
gl.enableVertexAttribArray(0);
gl.vertexAttribPointer(0, 2, gl.FLOAT, false, 0, 0);

const U = {};
function uniform(program, name) {
	const key = name;
	let table = U[key] || (U[key] = new Map());
	if (!table.has(program)) table.set(program, gl.getUniformLocation(program, name));
	return table.get(program);
}
function set1(p, n, v) { gl.uniform1f(uniform(p, n), v); }
function set2(p, n, a, b) { gl.uniform2f(uniform(p, n), a, b); }
function set3(p, n, v) { gl.uniform3f(uniform(p, n), v[0], v[1], v[2]); }
function seti(p, n, v) { gl.uniform1i(uniform(p, n), v); }

// --------------------------------------------------------------------------
// The palette table: the plugin's own bake, as a PNG.
// --------------------------------------------------------------------------
const paletteTex = gl.createTexture();
{
	const img = new Image();
	img.onload = () => {
		gl.bindTexture(gl.TEXTURE_2D, paletteTex);
		gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false);
		gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, gl.RGBA, gl.UNSIGNED_BYTE, img);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
	};
	img.src = 'palette-table.png';
}

// --------------------------------------------------------------------------
// The demo clip: an animated card drawn on a 2D canvas -- shapes and a
// wordmark orbiting slowly, so Engine A has outlines worth tracing without
// asking for the camera.
// --------------------------------------------------------------------------
const clip = document.createElement('canvas');
clip.width = 960; clip.height = 540;
const ctx = clip.getContext('2d');

function drawClip(t) {
	ctx.fillStyle = '#101014';
	ctx.fillRect(0, 0, clip.width, clip.height);

	const cx = clip.width / 2, cy = clip.height / 2;

	ctx.fillStyle = '#e8e8ee';
	ctx.beginPath();
	ctx.arc(cx + Math.cos(t * 0.4) * 210, cy + Math.sin(t * 0.53) * 110, 78, 0, Math.PI * 2);
	ctx.fill();

	ctx.lineWidth = 30;
	ctx.strokeStyle = '#d5d5de';
	ctx.beginPath();
	ctx.arc(cx + Math.cos(t * 0.31 + 2.1) * 240, cy + Math.sin(t * 0.42 + 1.2) * 130, 70, 0, Math.PI * 2);
	ctx.stroke();

	ctx.save();
	ctx.translate(cx + Math.cos(t * 0.24 + 4.0) * 200, cy + Math.sin(t * 0.35 + 3.1) * 120);
	ctx.rotate(t * 0.3);
	ctx.fillStyle = '#f0f0f4';
	ctx.fillRect(-60, -60, 120, 120);
	ctx.restore();

	ctx.font = '700 110px system-ui, sans-serif';
	ctx.textAlign = 'center';
	ctx.textBaseline = 'middle';
	ctx.fillStyle = '#ffffff';
	ctx.fillText('OUTRUN', cx, cy);
}

// Webcam, on request.
const video = document.createElement('video');
video.muted = true; video.playsInline = true;
let videoReady = false;
async function enableWebcam() {
	try {
		const stream = await navigator.mediaDevices.getUserMedia({ video: { width: 1280, height: 720 } });
		video.srcObject = stream;
		await video.play();
		videoReady = true;
		state.source = 1;
	} catch (e) {
		document.getElementById('webcam').checked = false;
		state.source = 0;
	}
}

const sourceTex = gl.createTexture();
gl.bindTexture(gl.TEXTURE_2D, sourceTex);
gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, 2, 2, 0, gl.RGBA, gl.UNSIGNED_BYTE,
	new Uint8Array(16));
gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);

// --------------------------------------------------------------------------
// Audio: a synth loop or the microphone, into 64 smoothed bins -- the same
// sqrt + fast-attack/slow-release shaping as the plugin's UpdateAudio().
// --------------------------------------------------------------------------
const audioLevel = new Float32Array(64);
let audioCtx = null, analyser = null, freqData = null, synthNodes = null;
let audioClock = -1;

function ensureAudio() {
	if (audioCtx) return;
	audioCtx = new (window.AudioContext || window.webkitAudioContext)();
	analyser = audioCtx.createAnalyser();
	analyser.fftSize = 256;
	analyser.smoothingTimeConstant = 0;
	freqData = new Uint8Array(analyser.frequencyBinCount);
	startSynth();
}

// A little four-on-the-floor: kick, hat, and a detuned saw bass. Enough
// spectrum to make the audio controls mean something without the microphone.
function startSynth() {
	stopSynth();
	const master = audioCtx.createGain();
	master.gain.value = 0.5;
	master.connect(analyser);

	const bpm = 120;
	const beat = 60 / bpm;
	let running = true;

	function schedule(at, beatIndex) {
		if (!running) return;
		// kick
		const osc = audioCtx.createOscillator();
		const env = audioCtx.createGain();
		osc.frequency.setValueAtTime(150, at);
		osc.frequency.exponentialRampToValueAtTime(45, at + 0.12);
		env.gain.setValueAtTime(1.0, at);
		env.gain.exponentialRampToValueAtTime(0.001, at + 0.25);
		osc.connect(env); env.connect(master);
		osc.start(at); osc.stop(at + 0.3);
		// bass note, every other beat
		if (beatIndex % 2 === 0) {
			const saw = audioCtx.createOscillator();
			const senv = audioCtx.createGain();
			saw.type = 'sawtooth';
			saw.frequency.value = beatIndex % 8 === 0 ? 55 : 73.4;
			senv.gain.setValueAtTime(0.18, at);
			senv.gain.exponentialRampToValueAtTime(0.001, at + beat * 0.9);
			saw.connect(senv); senv.connect(master);
			saw.start(at); saw.stop(at + beat);
		}
		// hat: a burst of noise
		const len = Math.floor(audioCtx.sampleRate * 0.05);
		const buffer = audioCtx.createBuffer(1, len, audioCtx.sampleRate);
		const data = buffer.getChannelData(0);
		for (let i = 0; i < len; ++i) data[i] = (Math.random() * 2 - 1) * Math.exp(-i / (len * 0.25));
		const noise = audioCtx.createBufferSource();
		noise.buffer = buffer;
		const henv = audioCtx.createGain();
		henv.gain.value = 0.12;
		noise.connect(henv); henv.connect(master);
		noise.start(at + beat * 0.5);
	}

	let nextBeat = audioCtx.currentTime + 0.05;
	let index = 0;
	const timer = setInterval(() => {
		if (!running) return;
		while (nextBeat < audioCtx.currentTime + 0.4) {
			schedule(nextBeat, index);
			nextBeat += beat;
			index += 1;
		}
	}, 100);

	synthNodes = { master, stop: () => { running = false; clearInterval(timer); master.disconnect(); } };
}

function stopSynth() {
	if (synthNodes) { synthNodes.stop(); synthNodes = null; }
}

async function enableMic() {
	ensureAudio();
	try {
		const stream = await navigator.mediaDevices.getUserMedia({ audio: true });
		const src = audioCtx.createMediaStreamSource(stream);
		stopSynth();
		src.connect(analyser);
		state.mic = true;
	} catch (e) {
		document.getElementById('mic').checked = false;
		state.mic = false;
		startSynth();
	}
}

function updateAudio(now) {
	if (!analyser) return;
	analyser.getByteFrequencyData(freqData);
	const dt = audioClock >= 0 && now > audioClock ? now - audioClock : 0;
	audioClock = now;
	const release = dt > 0 ? 1 - Math.exp(-dt / 0.15) : 1;
	for (let i = 0; i < 64; ++i) {
		const raw = Math.sqrt(freqData[i] / 255);
		if (raw >= audioLevel[i]) audioLevel[i] = raw;
		else audioLevel[i] += (raw - audioLevel[i]) * release;
	}
}

// --------------------------------------------------------------------------
// The frame.
// --------------------------------------------------------------------------
let phase = 0;
let last = -1;
let historyValid = false;
let stableCurrent = 0;
const curve = new Float32Array(49 * 2);

function draw(nowMs) {
	requestAnimationFrame(draw);
	const now = nowMs / 1000;
	const dt = last >= 0 ? Math.min(now - last, 0.25) : 0;
	last = now;
	phase += dt * map.speed(state.speed);

	const w = canvas.width = canvas.clientWidth * Math.min(devicePixelRatio, 2) | 0;
	const h = canvas.height = canvas.clientHeight * Math.min(devicePixelRatio, 2) | 0;
	if (w === 0 || h === 0) return;
	ensureTargets(w, h);

	if (state.audioOn || state.mic) updateAudio(now);
	else audioLevel.fill(0);

	let bass = 0;
	for (let i = 0; i < 8; ++i) bass += audioLevel[i];
	bass /= 8;
	const breakEffective = Math.min(1, state.breakAmount + state.audioBreak * bass);

	// Source texture.
	gl.bindTexture(gl.TEXTURE_2D, sourceTex);
	gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, true);
	if (state.source === 1 && videoReady)
		gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, gl.RGBA, gl.UNSIGNED_BYTE, video);
	else {
		drawClip(now);
		gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, gl.RGBA, gl.UNSIGNED_BYTE, clip);
	}
	gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false);

	gl.bindVertexArray(vao);
	gl.disable(gl.BLEND);

	const pass = (program, target) => {
		gl.useProgram(program);
		gl.bindFramebuffer(gl.FRAMEBUFFER, target ? target.fbo : null);
		gl.viewport(0, 0, target ? target.w : w, target ? target.h : h);
	};
	const bind = (unit, tex) => {
		gl.activeTexture(gl.TEXTURE0 + unit);
		gl.bindTexture(gl.TEXTURE_2D, tex);
	};

	// 1. copy
	pass(programs.copy, targets.copy);
	bind(0, sourceTex);
	seti(programs.copy, 'InputTexture', 0);
	gl.drawArrays(gl.TRIANGLES, 0, 3);
	gl.bindTexture(gl.TEXTURE_2D, targets.copy.tex);
	gl.generateMipmap(gl.TEXTURE_2D);

	const engineA = state.engine === 0;
	let stableTarget = stableCurrent;

	if (engineA) {
		// 2. edge
		pass(programs.edge, targets.edge);
		bind(0, targets.copy.tex);
		seti(programs.edge, 'CopyTexture', 0);
		set2(programs.edge, 'TexelSize', 1 / w, 1 / h);
		set1(programs.edge, 'Detail', map.detail(state.detail));
		set1(programs.edge, 'SourceMode', state.detectOn);
		gl.drawArrays(gl.TRIANGLES, 0, 3);

		// 3. stabilise
		const history = stableCurrent;
		stableTarget = 1 - stableCurrent;
		pass(programs.stabilise, targets.stable[stableTarget]);
		bind(0, targets.edge.tex);
		bind(1, targets.stable[history].tex);
		seti(programs.stabilise, 'EdgeTexture', 0);
		seti(programs.stabilise, 'HistoryTexture', 1);
		set1(programs.stabilise, 'Attack', map.attack(state.stability));
		set1(programs.stabilise, 'Release', map.release(state.stability));
		set1(programs.stabilise, 'Sensitivity', map.sensitivity(state.sensitivity));
		set1(programs.stabilise, 'Softness', map.softness(state.softness));
		set1(programs.stabilise, 'Reset', historyValid ? 0 : 1);
		gl.drawArrays(gl.TRIANGLES, 0, 3);
		stableCurrent = stableTarget;
		historyValid = true;
		gl.bindTexture(gl.TEXTURE_2D, targets.stable[stableTarget].tex);
		gl.generateMipmap(gl.TEXTURE_2D);
	}

	// 4. stroke
	const p = programs.stroke;
	pass(p, targets.stroke);
	bind(0, paletteTex);
	bind(1, targets.stable[stableTarget].tex);
	bind(2, targets.copy.tex);
	seti(p, 'PaletteTexture', 0);
	seti(p, 'StableTexture', 1);
	seti(p, 'CopyTexture', 2);
	set1(p, 'Engine', state.engine);
	set1(p, 'CentroidLod', Math.floor(Math.log2(Math.max(w, h))));
	set1(p, 'Trace', state.trace);
	set1(p, 'Aspect', w / h);
	set2(p, 'PictureSize', w, h);
	set1(p, 'WidthPx', map.width(state.width) * Math.min(devicePixelRatio, 2));
	set1(p, 'Core', state.core);
	set1(p, 'TraceAngle', state.traceAngle);
	set1(p, 'PathIndex', state.path);
	set1(p, 'PathScale', map.pathScale(state.pathScale));
	set1(p, 'PathDetail', map.pathDetail(state.pathDetail));
	set1(p, 'Horizon', map.horizon(state.horizon));
	set1(p, 'BreakMode', state.breakMode);
	set1(p, 'BreakAmount', breakEffective);
	set1(p, 'BreakSpread', state.breakSpread);
	set1(p, 'BreakHue', map.breakHue(state.breakHue));
	set1(p, 'ColourMode', state.colourMode);
	set1(p, 'PaletteIndex', state.palette);
	set3(p, 'Colour1', state.colour1);
	set3(p, 'Colour2', state.colour2);
	set1(p, 'Spread', map.spread(state.spread));
	set1(p, 'Saturation', map.saturation(state.saturation));
	set1(p, 'Brightness', map.brightness(state.brightness));
	set1(p, 'Phase', phase);
	gl.uniform1fv(uniform(p, 'Audio[0]'), audioLevel);
	set1(p, 'AudioLevel', state.audioLevel);
	if (state.engine === 1 && state.path === 1) {
		const scale = map.pathScale(state.pathScale);
		const detail = map.pathDetail(state.pathDetail);
		const a = detail * 0.5 + 1.0;
		const b = detail * 0.35 + 2.0;
		const aspect = w / h;
		for (let i = 0; i < 49; ++i) {
			const tau = (i / 48) * Math.PI * 2;
			curve[i * 2] = 0.5 + 0.5 * scale * Math.sin(a * tau + phase * Math.PI * 0.5 + Math.PI / 2) / aspect;
			curve[i * 2 + 1] = 0.5 + 0.5 * scale * Math.sin(b * tau);
		}
		gl.uniform2fv(uniform(p, 'Curve[0]'), curve);
	}
	gl.drawArrays(gl.TRIANGLES, 0, 3);
	gl.bindTexture(gl.TEXTURE_2D, targets.stroke.tex);
	gl.generateMipmap(gl.TEXTURE_2D);

	// 5. glow: three widening separable pairs, as in the plugin.
	const glowW = targets.glow[0].w, glowH = targets.glow[0].h;
	const gs = map.glowSize(state.glowSize);
	const stepX = gs * 0.001 * w / glowW;
	const stepY = gs * 0.001 * h / glowH;
	const downsampleLod = Math.log2(Math.max(1, w / glowW));
	const stages = [
		[-1, 0, stepX, 0, downsampleLod],
		[0, 1, 0, stepY, 0],
		[1, 0, stepX * 1.55, 0, 0],
		[0, 1, 0, stepY * 1.55, 0],
		[1, 0, stepX * 2.4, 0, 0],
		[0, 1, 0, stepY * 2.4, 0],
	];
	for (const [from, to, dx, dy, lod] of stages) {
		pass(programs.blur, targets.glow[to]);
		bind(0, from < 0 ? targets.stroke.tex : targets.glow[from].tex);
		seti(programs.blur, 'SourceTexture', 0);
		set2(programs.blur, 'Direction', dx, dy);
		set1(programs.blur, 'SourceLod', lod);
		gl.drawArrays(gl.TRIANGLES, 0, 3);
	}

	// 6. composite to the canvas.
	pass(programs.composite, null);
	bind(0, targets.stroke.tex);
	bind(1, targets.glow[1].tex);
	bind(2, targets.copy.tex);
	bind(3, targets.stable[stableTarget].tex);
	seti(programs.composite, 'LightTexture', 0);
	seti(programs.composite, 'GlowTexture', 1);
	seti(programs.composite, 'CopyTexture', 2);
	seti(programs.composite, 'StableTexture', 3);
	set1(programs.composite, 'Background', state.background);
	set1(programs.composite, 'Dim', 0.25);
	set1(programs.composite, 'Glow', map.glow(state.glow));
	gl.drawArrays(gl.TRIANGLES, 0, 3);
}

// --------------------------------------------------------------------------
// UI wiring.
// --------------------------------------------------------------------------
const $ = id => document.getElementById(id);

function bindSlider(id, key) {
	$(id).addEventListener('input', e => { state[key] = parseFloat(e.target.value); });
}
function bindSelect(id, key) {
	$(id).addEventListener('change', e => { state[key] = parseInt(e.target.value, 10); });
}

bindSelect('engine', 'engine');
bindSelect('path', 'path');
bindSelect('palette', 'palette');
bindSelect('breakmode', 'breakMode');
bindSelect('background', 'background');
bindSelect('colourmode', 'colourMode');
bindSlider('width', 'width');
bindSlider('core', 'core');
bindSlider('breakamount', 'breakAmount');
bindSlider('breakspread', 'breakSpread');
bindSlider('breakhue', 'breakHue');
bindSlider('speed', 'speed');
bindSlider('spreadc', 'spread');
bindSlider('glowamt', 'glow');
bindSlider('glowsize', 'glowSize');
bindSlider('audiolevel', 'audioLevel');
bindSlider('audiobreak', 'audioBreak');
bindSlider('sensitivity', 'sensitivity');
bindSlider('pathdetail', 'pathDetail');

$('engine').addEventListener('change', () => { historyValid = false; syncPanels(); });
$('webcam').addEventListener('change', e => {
	if (e.target.checked) enableWebcam();
	else { state.source = 0; }
});
$('mic').addEventListener('change', e => {
	if (e.target.checked) enableMic();
	else { state.mic = false; ensureAudio(); startSynth(); }
});

function syncPanels() {
	$('panel-a').style.display = state.engine === 0 ? '' : 'none';
	$('panel-b').style.display = state.engine === 1 ? '' : 'none';
}
syncPanels();

// Presets: the plugin's own table, abbreviated to the demo's controls.
const presets = {
	grid:   { engine: 1, path: 0, palette: 2, breakMode: 0, breakAmount: 0, speed: 0.25, glow: 0.6, audioLevel: 0 },
	sign:   { engine: 0, palette: 14, breakMode: 0, breakAmount: 0, width: 0.45, core: 0.7, glow: 0.65 },
	echo:   { engine: 0, palette: 6, breakMode: 1, breakAmount: 0.5, breakSpread: 0.5, breakHue: 0.4 },
	scan:   { engine: 0, palette: 9, breakMode: 3, breakAmount: 0.6, audioBreak: 0.6, speed: 0.35 },
	tunnel: { engine: 1, path: 5, palette: 5, breakMode: 0, breakAmount: 0, glow: 0.7 },
	scope:  { engine: 1, path: 7, palette: 13, audioLevel: 0.8, breakMode: 0, breakAmount: 0 },
};
for (const [name, values] of Object.entries(presets)) {
	$('preset-' + name).addEventListener('click', () => {
		Object.assign(state, values);
		// Reflect into the controls that exist.
		for (const [k, v] of Object.entries(values)) {
			const el = document.querySelector(`[data-key="${k}"]`);
			if (el) el.value = v;
		}
		historyValid = false;
		syncPanels();
	});
}

// Audio starts on the first gesture, as browsers require.
const kick = () => {
	ensureAudio();
	if (audioCtx.state === 'suspended') audioCtx.resume();
	window.removeEventListener('pointerdown', kick);
};
window.addEventListener('pointerdown', kick);

requestAnimationFrame(draw);
