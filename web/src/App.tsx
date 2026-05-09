import { protobufPackage } from "./proto/dya/trackpad/trackpad";

export function App() {
  return (
    <main className="app">
      <h1>Cirque Trackpad Studio UI</h1>
      <p>This UI is published with GitHub Pages for module testing and visualization.</p>
      <p>
        Protocol package: <code>{protobufPackage}</code>
      </p>
      <p>
        To control the trackpad live, open this page and connect from ZMK Studio RPC transport
        implementation.
      </p>
    </main>
  );
}
