# Debugging Frida Agent

The Frida agent runs as a shared library injected into a subprocess, which makes traditional debugging difficult. To simplify this, we have implemented a "Wait for Debugger" mechanism.

## 1. Enable Debug Mode

Set the environment variable `ADA_WAIT_FOR_DEBUGGER=1` when running your target application.

```bash
export ADA_WAIT_FOR_DEBUGGER=1
./your_target_app
```

When `ADA_WAIT_FOR_DEBUGGER=1` is set, the system will pause at two stages:

1.  **Controller**: Pauses before spawning the child process.
2.  **Agent**: Pauses immediately upon initialization in the child process.

You will see output like this:

```
[Controller] Waiting for debugger... (PID: 12345)
...
[Agent] Waiting for debugger... (PID: 12346)
...
```

## 2. Attach with VS Code

You can attach to the paused process directly from VS Code.

1.  Open your `.vscode/launch.json`.
2.  Add the following configuration:

```json
{
    "name": "Attach to Agent",
    "type": "lldb",
    "request": "attach",
    "pid": "${command:pickProcess}",
    "stopOnEntry": true
}
```

3.  Start your application with `ADA_WAIT_FOR_DEBUGGER=1`.
4.  Wait for the `[Agent] Waiting for debugger...` message.
5.  In VS Code, select **"Attach to Agent"** from the Run and Debug view.
6.  Enter the PID printed by the agent (or search for your process name).
7.  Once attached, the debugger will likely be paused. You may need to press **Continue (F5)** to resume execution.

## 3. Attach with LLDB (Command Line)

If you prefer the command line:

1.  Start your application with `ADA_WAIT_FOR_DEBUGGER=1`.
2.  In a new terminal, run:

```bash
lldb -p <PID>
```

3.  Once attached, type `c` or `continue` to resume the process.

## Troubleshooting

-   **Permissions**: On macOS, you might need to run LLDB with `sudo` or ensure your user has debugging privileges (DevToolsSecurity).
-   **Timeout**: If VS Code fails to attach, try attaching via command line `lldb` first to verify permissions.

## 4. Simultaneous Debugging (Host + Agent)

Yes, you can run two debug sessions in VS Code simultaneously! This is perfect for debugging the interaction between your main application and the agent.

### Step 1: Configure the Host Launch
Update your host launch configuration (e.g., "Rust: Debug tracer-backend/tracer") to include `ADA_WAIT_FOR_DEBUGGER=1`.

```json
{
  "name": "Rust: Debug tracer-backend/tracer",
  "type": "lldb",
  "request": "launch",
  "cargo": {
    "args": [
      "run",
      "--bin=tracer",
      "--package=tracer_backend"
    ]
  },
  "cwd": "${workspaceFolder}",
  "args": [
    "spawn",
    "${workspaceFolder}/target/debug/tracer_backend/test/test_cli"
  ],
  "env": {
    "RUST_LOG": "debug",
    "ADA_AGENT_RPATH_SEARCH_PATHS": "${workspaceFolder}/target/debug/tracer_backend/lib",
    "ADA_WAIT_FOR_DEBUGGER": "1"  // <--- Add this line
  },
  "terminal": "console"
}
```

### Step 2: Configure the Agent Attach
Ensure you have the "Attach to Agent" configuration in your `launch.json`:

```json
{
    "name": "Attach to Agent",
    "type": "lldb",
    "request": "attach",
    "pid": "${command:pickProcess}"
}
```

### Step 3: Launch and Attach
1.  **Start the Host**: Select "Rust: Debug tracer-backend/tracer" and press F5.
2.  **Wait for Pause**: The terminal will show `[Agent] Waiting for debugger... (PID: 12345)`.
3.  **Attach to Agent**:
    *   Switch the debug configuration dropdown to "Attach to Agent".
    *   Press F5 (or the Play button).
    *   Select the PID printed in the terminal (e.g., `12345`) or type the process name.
4.  **Debug Both**: You now have two debug sessions running! You can switch between them using the "Call Stack" view or the debug toolbar dropdown.

