// TypeEasy Debug Adapter
// Bridges VS Code DAP <-> our line-based JSON protocol over TCP (port 4711).
//
// Launch flow:
//   1) initializeRequest -> capabilities + 'initialized' event to client
//      (we DO NOT yet have a connection to the interpreter; we wait for launch)
//   2) launchRequest:
//      - spawn `docker compose run --rm -p PORT:PORT -v $PWD/typeeasycode:/code
//        typeeasy --debug-port PORT /code/<basename(program)>`
//      - poll-connect to localhost:PORT
//      - swallow the interpreter's "initialized" line (it's our own protocol)
//      - tell VS Code we are 'initialized' so it sends setBreakpoints
//   3) setBreakPointsRequest -> forward to interpreter
//   4) configurationDoneRequest -> send {"cmd":"start"} to interpreter
//   5) Forward stop events -> StoppedEvent; forward stack/vars on demand.

const path = require('path');
const cp = require('child_process');
const net = require('net');
const {
  LoggingDebugSession, DebugSession, InitializedEvent, TerminatedEvent, StoppedEvent,
  OutputEvent, Thread, StackFrame, Source, Scope, Handles
} = require('@vscode/debugadapter');

const THREAD_ID = 1;
const POLL_INTERVAL_MS = 150;
const POLL_TIMEOUT_MS = 30000;

class TypeEasyDebugSession extends DebugSession {
  constructor() {
    super();
    this.setDebuggerLinesStartAt1(true);
    this.setDebuggerColumnsStartAt1(true);

    this._socket = null;
    this._rxBuf = '';
    this._pendingResolvers = []; // FIFO of resolvers awaiting any response line
    this._dockerProc = null;
    this._programPath = null;     // host absolute path to the .te
    this._programBasename = null; // e.g. link_to_objects.te
    this._variableHandles = new Handles();
  }

  // ---- DAP requests ----

  initializeRequest(response, args) {
    response.body = response.body || {};
    response.body.supportsConfigurationDoneRequest = true;
    response.body.supportsEvaluateForHovers = true;
    response.body.supportsStepInTargetsRequest = false;
    this.sendResponse(response);
    // 'initialized' is sent AFTER we've connected to the interpreter
    // (in launchRequest), because BPs must be forwarded to a live socket.
  }

  async launchRequest(response, args) {
    this._programPath = args.program;
    this._programBasename = path.basename(this._programPath);
    const port = args.port || 4711;
    const stopOnEntry = !!args.stopOnEntry;
    this._stopOnEntry = stopOnEntry;
    const attachOnly = args.attachOnly === true;

    this.sendEvent(new OutputEvent(`[typeeasy-dap] launching: ${this._programBasename}\n`, 'console'));

    // Resolve the host workspace folder containing typeeasycode/.
    // Convention: program is inside <workspace>/typeeasycode/<file>.te
    const programDir = path.dirname(this._programPath);
    const workspaceRoot = path.resolve(programDir, '..'); // parent of typeeasycode
    this._workspaceRoot = workspaceRoot;
    this.sendEvent(new OutputEvent(`[typeeasy-dap] workspaceRoot=${workspaceRoot}\n`, 'console'));

    if (!attachOnly) {
      // Spawn docker compose run with the interpreter in --debug-port mode.
      const dockerArgs = [
        'compose', 'run', '--rm',
        '-p', `${port}:${port}`,
        '-v', `${workspaceRoot.replace(/\\/g, '/')}/typeeasycode:/code`,
        '--entrypoint', '/typeeasy/typeeasy',
        'typeeasy',
        '--debug-port', String(port),
        `/code/${this._programBasename}`
      ];
      this.sendEvent(new OutputEvent(`[typeeasy-dap] spawn: docker ${dockerArgs.join(' ')}\n`, 'console'));

      const env = Object.assign({}, process.env, {
        MSYS_NO_PATHCONV: '1',
        MSYS2_ARG_CONV_EXCL: '*'
      });

      const dockerCmd = process.platform === 'win32' ? 'docker.exe' : 'docker';
      try {
        this._dockerProc = cp.spawn(dockerCmd, dockerArgs, {
          cwd: workspaceRoot,
          env,
          shell: false,
          windowsHide: true
        });
      } catch (e) {
        this.sendEvent(new OutputEvent(`[typeeasy-dap] spawn threw: ${e.message}\n`, 'stderr'));
        this.sendErrorResponse(response, 1002, `spawn failed: ${e.message}`);
        return;
      }

      this._dockerProc.on('error', err => {
        this.sendEvent(new OutputEvent(`[typeeasy-dap] spawn error: ${err.message}\n`, 'stderr'));
      });

      this._dockerProc.stdout.on('data', d => {
        this.sendEvent(new OutputEvent(d.toString(), 'stdout'));
      });
      this._dockerProc.stderr.on('data', d => {
        this.sendEvent(new OutputEvent(d.toString(), 'stderr'));
      });
      this._dockerProc.on('exit', (code) => {
        this.sendEvent(new OutputEvent(`[typeeasy-dap] interpreter exited code=${code}\n`, 'console'));
        this.sendEvent(new TerminatedEvent());
      });
    } else {
      this.sendEvent(new OutputEvent(`[typeeasy-dap] attachOnly: not spawning docker. Connect interpreter manually.\n`, 'console'));
    }

    // Connect TCP (poll because container takes time to listen).
    try {
      await this._connectWithRetry('127.0.0.1', port);
    } catch (e) {
      this.sendEvent(new OutputEvent(`[typeeasy-dap] connect error: ${e.message}\n`, 'stderr'));
      this.sendErrorResponse(response, 1001, `Could not connect to interpreter on port ${port}: ${e.message}`);
      return;
    }
    this.sendEvent(new OutputEvent(`[typeeasy-dap] connected to interpreter on port ${port}\n`, 'console'));

    // The interpreter sends {"event":"initialized"} on connect; _handleEvent
    // will see it and ignore it (only stopped/terminated are acted on). We do
    // NOT _readLine() here, because that would race with _dispatchLine routing
    // event-typed lines to _handleEvent.

    // Now ask VS Code for breakpoints + configurationDone.
    this.sendEvent(new InitializedEvent());
    this.sendResponse(response);
  }

  configurationDoneRequest(response, _args) {
    this._sockSend({ cmd: 'start' });
    this._readLine().then(_ack => {
      // After 'start', the interpreter runs until BP / end.
      // Start an async loop reading async events (stopped / terminated).
      this._eventLoop();
      this.sendResponse(response);
      if (this._stopOnEntry) {
        // We can simulate a stop on entry by sending 'pause' + step_in;
        // for v1 we simply ignore stopOnEntry until a real BP hits.
      }
    });
  }

  setBreakPointsRequest(response, args) {
    const lines = (args.breakpoints || args.lines || []).map(b =>
      typeof b === 'number' ? b : b.line);
    const file = args.source && args.source.path ? path.basename(args.source.path) : '';
    this._sockSend({ cmd: 'set_breakpoints', file, lines });
    this._readLine().then(_resp => {
      response.body = {
        breakpoints: lines.map(l => ({ verified: true, line: l }))
      };
      this.sendResponse(response);
    });
  }

  threadsRequest(response) {
    response.body = { threads: [new Thread(THREAD_ID, 'main')] };
    this.sendResponse(response);
  }

  async stackTraceRequest(response, _args) {
    this._sockSend({ cmd: 'stack' });
    const line = await this._readLine();
    let frames = [];
    try {
      const obj = JSON.parse(line);
      const src = new Source(this._programBasename, this._programPath);
      frames = (obj.frames || []).map((f, idx) =>
        new StackFrame(idx, f.name || `<frame${idx}>`, src, f.line || 1));
    } catch (e) {
      this.sendEvent(new OutputEvent(`[typeeasy-dap] bad stack: ${e.message}\n`, 'stderr'));
    }
    response.body = { stackFrames: frames, totalFrames: frames.length };
    this.sendResponse(response);
  }

  scopesRequest(response, _args) {
    response.body = {
      scopes: [
        new Scope('Locals', this._variableHandles.create('locals'), false)
      ]
    };
    this.sendResponse(response);
  }

  async variablesRequest(response, args) {
    const ref = this._variableHandles.get(args.variablesReference);
    let cmd;
    if (ref === 'locals') {
      cmd = { cmd: 'vars' };
    } else if (ref && typeof ref === 'object' && ref.kind === 'children') {
      cmd = { cmd: 'get_children', ref: ref.remoteRef };
    } else {
      response.body = { variables: [] };
      this.sendResponse(response);
      return;
    }
    this._sockSend(cmd);
    const line = await this._readLine();
    let vars = [];
    try {
      const obj = JSON.parse(line);
      vars = (obj.vars || []).map(v => {
        const remoteRef = v.ref | 0;
        const variablesReference = remoteRef > 0
          ? this._variableHandles.create({ kind: 'children', remoteRef })
          : 0;
        return {
          name: v.name,
          value: String(v.value),
          type: v.type,
          variablesReference
        };
      });
    } catch (e) {
      this.sendEvent(new OutputEvent(`[typeeasy-dap] bad vars: ${e.message}\n`, 'stderr'));
    }
    response.body = { variables: vars };
    this.sendResponse(response);
  }

  continueRequest(response, _args) {
    this._sockSend({ cmd: 'continue' });
    response.body = { allThreadsContinued: true };
    this.sendResponse(response);
  }

  async evaluateRequest(response, args) {
    /* Used for hover, watch, and the debug console REPL. */
    const expr = (args && args.expression) ? String(args.expression).trim() : '';
    const ctx  = (args && args.context) || 'hover';
    if (!expr) {
      response.body = { result: '', variablesReference: 0 };
      this.sendResponse(response);
      return;
    }
    this._sockSend({ cmd: 'eval', expr, context: ctx });
    const line = await this._readLine();
    let result = '', remoteRef = 0, type = '';
    try {
      const obj = JSON.parse(line);
      result = String(obj.value != null ? obj.value : '');
      type = obj.type || '';
      remoteRef = obj.ref | 0;
    } catch (e) {
      result = '<eval error>';
    }
    const variablesReference = remoteRef > 0
      ? this._variableHandles.create({ kind: 'children', remoteRef })
      : 0;
    response.body = { result, type, variablesReference };
    this.sendResponse(response);
  }

  nextRequest(response, _args)    { this._sockSend({ cmd: 'next' });     this.sendResponse(response); }
  stepInRequest(response, _args)  { this._sockSend({ cmd: 'step_in' });  this.sendResponse(response); }
  stepOutRequest(response, _args) { this._sockSend({ cmd: 'step_out' }); this.sendResponse(response); }
  pauseRequest(response, _args)   { this._sockSend({ cmd: 'pause' });    this.sendResponse(response); }

  disconnectRequest(response, _args) {
    try { this._sockSend({ cmd: 'disconnect' }); } catch (_) {}
    try { this._socket && this._socket.destroy(); } catch (_) {}
    if (this._dockerProc && !this._dockerProc.killed) {
      try { this._dockerProc.kill(); } catch (_) {}
    }
    this.sendResponse(response);
  }

  // ---- transport ----

  _connectWithRetry(host, port) {
    return new Promise((resolve, reject) => {
      const start = Date.now();
      const tryOnce = () => {
        const s = net.connect({ host, port }, () => {
          this._socket = s;
          s.on('data', d => this._onData(d));
          s.on('error', e => this.sendEvent(new OutputEvent(`[typeeasy-dap] sock err: ${e.message}\n`, 'stderr')));
          s.on('close', () => { /* terminated event arrives via docker exit */ });
          resolve();
        });
        s.on('error', _e => {
          s.destroy();
          if (Date.now() - start > POLL_TIMEOUT_MS) {
            return reject(new Error('timeout waiting for interpreter to listen'));
          }
          setTimeout(tryOnce, POLL_INTERVAL_MS);
        });
      };
      tryOnce();
    });
  }

  _onData(buf) {
    this._rxBuf += buf.toString('utf8');
    let idx;
    while ((idx = this._rxBuf.indexOf('\n')) >= 0) {
      const line = this._rxBuf.slice(0, idx);
      this._rxBuf = this._rxBuf.slice(idx + 1);
      this._dispatchLine(line);
    }
  }

  _dispatchLine(line) {
    // Async events ("event"): handled by event loop or stopped handler.
    // Responses ("resp"): wake up next pending resolver.
    if (line.startsWith('{"event"')) {
      // route to event handler
      this._handleEvent(line);
      return;
    }
    const r = this._pendingResolvers.shift();
    if (r) r(line);
  }

  _readLine() {
    return new Promise(resolve => this._pendingResolvers.push(resolve));
  }

  _sockSend(obj) {
    if (!this._socket) return;
    this._socket.write(JSON.stringify(obj) + '\n');
  }

  _handleEvent(line) {
    try {
      const obj = JSON.parse(line);
      if (obj.event === 'stopped') {
        this.sendEvent(new StoppedEvent(obj.reason || 'breakpoint', THREAD_ID));
      } else if (obj.event === 'terminated') {
        this.sendEvent(new TerminatedEvent());
      } else if (obj.event === 'output') {
        const cat = obj.category === 'stderr' ? 'stderr' : 'stdout';
        this.sendEvent(new OutputEvent(obj.text || '', cat));
      }
    } catch (_) { /* ignore malformed */ }
  }

  _eventLoop() {
    /* events come asynchronously through _onData -> _handleEvent;
     * no explicit loop needed. Kept for symmetry/future use. */
  }
}

module.exports = { TypeEasyDebugSession };
