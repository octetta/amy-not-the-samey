    var web_audio_buffer = null;
    var amy_play_message = null;
    var amy_sample_rate = null;
    var amy_start_web = null;
    var startDate = new Date();
    var startTime = startDate.getTime();
    // you can only start calling c++ functions once emscripten's "runtime" has started
    Module.onRuntimeInitialized = function() {
        web_audio_buffer = Module.cwrap(
            'web_audio_buffer', 'number', ['number', 'number']
        );
        amy_play_message = Module.cwrap(
            'amy_play_message', null, ['string']
        );
        amy_start_web = Module.cwrap(
            'amy_start_web', null, ['number']
        );
        amy_sample_rate = Module.cwrap(
            'amy_sample_rate', 'number', ['number']
        );
    }


    var dataHeap = null;
    var dataPtr = null;

    var data = new Float32Array();

    // l and r are float arrays for the left and right channels that need to be filled
    function audioCallback(l) {
        // lazy loading of the audio buffer we use to talk to c++/emscripten
        if(dataHeap == null) {
            data = new Float32Array(l.length);
            // Get data byte size, allocate memory on Emscripten heap, and get pointer
            var nDataBytes = data.length * data.BYTES_PER_ELEMENT;
            dataPtr = Module._malloc(nDataBytes);

            // Copy data to Emscripten heap (directly accessed from Module.HEAPU8)
            dataHeap = new Uint8Array(Module.HEAPU8.buffer, dataPtr, nDataBytes);
            dataHeap.set(new Uint8Array(data.buffer));
        }

        // now we actually call the C++
        web_audio_buffer(dataHeap.byteOffset, l.length);

        // turn the emscripten heap array to something we can work with in javascript
        // allocating on the audio thread, I know!
        var result = new Float32Array(dataHeap.buffer, dataHeap.byteOffset, data.length);

        // deinterleave the result
        for(i = 0; i < l.length; i++) {
            l[i] = result[i];
        }
    }


var audioRunning = false;
var scriptNode = null;
var source = null;
var audioCtx = null;

function setupAudio(fn) {
  // Create AudioContext and buffer source
  var AudioContext = window.AudioContext // Default
    || window.webkitAudioContext // Safari and old versions of Chrome
    || false; 
    
    if (!AudioContext) {
        // Do whatever you want using the Web Audio API
        alert("Sorry, but the Web Audio API is not supported by your browser.");
    }
  audioCtx = new AudioContext({sampleRate: 44100});
  // JOE
  console.log("before set sample...");
  audioCtx.sampleRate = 44100.0;
  console.log("after set sample...");
  // END
  source = audioCtx.createBufferSource();

  // buff size, ins, outs
  scriptNode = audioCtx.createScriptProcessor(256, 0, 1);
  scriptNode.onaudioprocess = function(audioProcessingEvent) {
    fn(audioProcessingEvent.outputBuffer.getChannelData(0)); 
  };
}
function millis() {
    var date_now = new Date (); 
    var time_now = date_now.getTime (); 
    var time_diff = time_now - startTime; 
    return time_diff;
    //var seconds_elapsed = Math.floor ( time_diff  );
}
function reset() {
    if(amy_started) {
        var code = "S65";
        amy_play_message(code);
    }
}

function wire(code) {
    if(amy_started) {
        amy_play_message(code);
    }
}
var amy_started = false;
function startAudio() {

  amy_start_web();
  amy_started = true;
  if(audioRunning) return;
  setupAudio(audioCallback);
  scriptNode.connect(audioCtx.destination);
  source.start();
  audioRunning = true;
  document.getElementById("startStop").innerHTML = "quiet";
  document.getElementById("startStop").href = "javascript: stopAudio();";
}

function stopAudio() {
    audioRunning = false;
    amy_started = false;
    audioCtx.suspend().then(function() { 
        document.getElementById("startStop").innerHTML = "noise";
        document.getElementById("startStop").href = "javascript: startAudio();";
    });
}
