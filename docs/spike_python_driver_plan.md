# Plan: Adding a Python-based Spike driver to Joshua

This plan lays out incremental steps to bring LEGO Spike (Pybricks) support into Joshua's hardware abstraction layer, with a test/check after each step.

## Steps and checks

1) **Add Python-friendly schema hooks**  
   - Keep `comm_type` about transport (e.g., USB/BLE/Serial) with fields like port/hub id/baud.  
   - Add a separate driver hint (e.g., `driver_binding: PYTHON` or `driver: "spike_py"`) on the actuator/encoder so factories can choose a Python driver.  
   - Keep it generic so other Python devices can plug in later without schema churn.  
   - **Test:** Build protos (`bazel build //config/proto:...`) and validate a sample pbtxt parses.

2) **Define Python driver interfaces**  
   - Add Python equivalents of ActionInterface/PerceptionInterface (start/stop/set/get) or a thin binding matching existing semantics, designed for reuse across devices.  
   - **Test:** Import the interfaces in a REPL/unit test; run `python -m py_compile` on the new module.

3) **Implement Spike Python drivers**  
   - Implement Spike motor (and optional encoder) drivers using pybricksdev or a lightweight LWP client.  
   - Handle connect/start/stop, scaling/clamping, basic error reporting.  
   - **Test:** Unit tests with stubbed transport asserting commands/reads; similar to `python ros2/python_bridge_backend_test.py`.

4) **Wire factories**  
   - Update action/perception factories (or add Python factory hooks) to return Python drivers when CommType/Type is a Python-backed device (e.g., Spike).  
   - **Test:** Factory unit test that loads a Spike pbtxt snippet and asserts a Spike driver is instantiated.

5) **Lifecycle and transport handling**  
   - Support USB and BLE selection; add reconnect/backoff; clean shutdown.  
   - **Test:** Mock transport to simulate failures/reconnect and verify state/logging.

6) **Integrate with ROS nodes**  
   - Make actuator_subscriber/encoder_publisher (or a Python node) use the Python drivers via the factory.  
   - **Test:** Run the nodes with a Spike config in mock/loopback mode; ensure topics are created and no runtime errors.

7) **Hardware smoke test**  
   - With Pybricks firmware + helper on the hub, run the nodes, send a command, and verify motor movement (and encoder if implemented).  
   - **Test:** Manual hardware runbook; capture logs (SET/GET responses).

8) **Docs and examples**  
   - Add a Spike pbtxt preset and README instructions for BLE/USB setup, helper upload, and run commands.  
   - **Test:** Follow the README steps end-to-end on a fresh shell to confirm they work.

## Mixing Python Spike nodes with C++ nodes
- You can include Python-based Spike nodes alongside existing C++ nodes in one pbtxt: give each node its own `node_type`, `id`, topics, and QoS.
- Launch each process separately (e.g., Python Spike node/bridge and C++ actuator/encoder nodes) after sourcing ROS; ensure the Python env has its deps (pybricksdev, etc.).
- Avoid topic/node_id collisions and ensure only one process owns the Spike link at a time.
