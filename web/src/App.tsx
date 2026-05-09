import { useContext, useMemo, useState } from "react";
import { connect as serialConnect } from "@zmkfirmware/zmk-studio-ts-client/transport/serial";
import {
  ZMKAppContext,
  ZMKConnection,
  ZMKCustomSubsystem,
} from "@cormoran/zmk-studio-react-hook";
import { Request, Response, Sensitivity, type TrackpadDevice } from "./proto/dya/trackpad/trackpad";

const SUBSYSTEM_CANDIDATES = [
  "dya__trackpad",
  "dya_trackpad",
  "zmk__trackpad",
  "trackpad",
];

export function App() {
  return (
    <div className="app">
      <h1>Cirque Trackpad Studio UI</h1>
      <ZMKConnection
        renderDisconnected={({ connect, isLoading, error }) => (
          <section className="card">
            {isLoading && <p>Connecting...</p>}
            {error && <p className="error">{error}</p>}
            <button disabled={isLoading} onClick={() => connect(serialConnect)}>
              Connect Serial
            </button>
          </section>
        )}
        renderConnected={({ disconnect, deviceName }) => (
          <>
            <section className="card">
              <p>Connected: {deviceName}</p>
              <button onClick={disconnect}>Disconnect</button>
            </section>
            <TrackpadSection />
          </>
        )}
      />
    </div>
  );
}

function TrackpadSection() {
  const zmkApp = useContext(ZMKAppContext);
  const [devices, setDevices] = useState<TrackpadDevice[]>([]);
  const [selectedId, setSelectedId] = useState<number | null>(null);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string>("");

  const subsystem = useMemo(() => {
    if (!zmkApp) {
      return null;
    }
    for (const id of SUBSYSTEM_CANDIDATES) {
      const found = zmkApp.findSubsystem(id);
      if (found) {
        return found;
      }
    }
    const available = (zmkApp.state.connection as any)?.subsystems ?? [];
    const bestEffort = available.find((s: any) =>
      String(s?.identifier ?? "").toLowerCase().includes("track")
    );
    if (bestEffort) {
      return bestEffort;
    }
    return null;
  }, [zmkApp]);

  if (!zmkApp) {
    return null;
  }

  if (!subsystem) {
    return (
      <section className="card">
        <p className="error">
          Trackpad subsystem not found. Tried: {SUBSYSTEM_CANDIDATES.join(", ")}
        </p>
      </section>
    );
  }

  const call = async (request: Request) => {
    const conn = zmkApp.state.connection;
    if (!conn) {
      throw new Error("No connection");
    }
    const service = new ZMKCustomSubsystem(conn, subsystem.index);
    const payload = Request.encode(request).finish();
    const respPayload = await service.callRPC(payload);
    if (!respPayload) {
      throw new Error("Empty RPC response");
    }
    const resp = Response.decode(respPayload);
    if (resp.error) {
      throw new Error(resp.error.message);
    }
    return resp;
  };

  const loadDevices = async () => {
    setBusy(true);
    setError("");
    try {
      const resp = await call(Request.create({ listDevices: {} }));
      const nextDevices = resp.listDevices?.devices ?? [];
      setDevices(nextDevices);
      if (nextDevices.length > 0 && selectedId == null) {
        setSelectedId(nextDevices[0].id);
      }
    } catch (e) {
      setError(e instanceof Error ? e.message : "RPC failed");
    } finally {
      setBusy(false);
    }
  };

  const refreshOne = async () => {
    if (selectedId == null) {
      return;
    }
    setBusy(true);
    setError("");
    try {
      const resp = await call(Request.create({ getDevice: { id: selectedId } }));
      const device = resp.getDevice?.device;
      if (!device) {
        throw new Error("Device not found");
      }
      setDevices((prev) => {
        const idx = prev.findIndex((d) => d.id === device.id);
        if (idx < 0) {
          return [...prev, device];
        }
        const next = [...prev];
        next[idx] = device;
        return next;
      });
    } catch (e) {
      setError(e instanceof Error ? e.message : "RPC failed");
    } finally {
      setBusy(false);
    }
  };

  const setSleep = async (enabled: boolean) => {
    if (selectedId == null) {
      return;
    }
    setBusy(true);
    setError("");
    try {
      await call(Request.create({ setSleep: { id: selectedId, enabled } }));
      await refreshOne();
    } catch (e) {
      setError(e instanceof Error ? e.message : "RPC failed");
      setBusy(false);
    }
  };

  const reset = async () => {
    if (selectedId == null) {
      return;
    }
    setBusy(true);
    setError("");
    try {
      await call(Request.create({ resetDevice: { id: selectedId } }));
      await refreshOne();
    } catch (e) {
      setError(e instanceof Error ? e.message : "RPC failed");
      setBusy(false);
    }
  };

  const selected = devices.find((d) => d.id === selectedId) ?? null;

  return (
    <section className="card">
      <h2>Trackpad RPC</h2>
      <div className="row">
        <button disabled={busy} onClick={loadDevices}>
          List Devices
        </button>
        <button disabled={busy || selectedId == null} onClick={refreshOne}>
          Refresh Selected
        </button>
        <button disabled={busy || selectedId == null} onClick={() => setSleep(true)}>
          Sleep ON
        </button>
        <button disabled={busy || selectedId == null} onClick={() => setSleep(false)}>
          Sleep OFF
        </button>
        <button disabled={busy || selectedId == null} onClick={reset}>
          Reset
        </button>
      </div>

      {error && <p className="error">{error}</p>}

      {devices.length > 0 && (
        <label>
          Device:
          <select value={selectedId ?? ""} onChange={(e) => setSelectedId(Number(e.target.value))}>
            {devices.map((d) => (
              <option key={d.id} value={d.id}>
                {d.id}: {d.name}
              </option>
            ))}
          </select>
        </label>
      )}

      {selected && (
        <pre>{JSON.stringify(formatDevice(selected), null, 2)}</pre>
      )}
    </section>
  );
}

function formatDevice(d: TrackpadDevice) {
  return {
    ...d,
    sensitivity: Sensitivity[d.sensitivity] ?? d.sensitivity,
  };
}
