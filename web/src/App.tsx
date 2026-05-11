import { useContext, useMemo, useState } from "react";
import { connect as serialConnect } from "@zmkfirmware/zmk-studio-ts-client/transport/serial";
import { connect as gattConnect } from "@zmkfirmware/zmk-studio-ts-client/transport/gatt";
import {
  ZMKAppContext,
  ZMKConnection,
  ZMKCustomSubsystem,
} from "@cormoran/zmk-studio-react-hook";
import { Request, Response, Sensitivity, type TrackpadDevice } from "./proto/dya/trackpad/trackpad";

type TrackpadRequest = Request & { setDevice?: { device: TrackpadDevice } };

const SUBSYSTEM_CANDIDATES = [
  "dya__trackpad",
  "dya_trackpad",
  "zmk__trackpad",
  "trackpad",
];

const BOOL_FIELDS: Array<keyof TrackpadDevice> = [
  "rotate90",
  "xInvert",
  "yInvert",
  "noTaps",
  "noSecondaryTap",
  "absoluteGestures",
  "tapToClick",
  "doubleTapDrag",
  "reverseCircularScroll",
];

const NUMBER_FIELDS: Array<keyof TrackpadDevice> = [
  "xAxisZMin",
  "yAxisZMin",
  "touchZThreshold",
  "xMax",
  "yMax",
  "edgeScrollMargin",
  "pointerDivisor",
  "tapTimeoutMs",
  "doubleTapMs",
  "tapMoveThreshold",
  "scrollStep",
];

function encodeVarint(value: number): number[] {
  const bytes: number[] = [];
  let next = value >>> 0;
  while (next > 0x7f) {
    bytes.push((next & 0x7f) | 0x80);
    next >>>= 7;
  }
  bytes.push(next);
  return bytes;
}

function encodeUint32Field(fieldNumber: number, value: number): number[] {
  return [...encodeVarint(fieldNumber << 3), ...encodeVarint(value)];
}

function encodeBoolField(fieldNumber: number, value: boolean): number[] {
  return [...encodeVarint(fieldNumber << 3), value ? 1 : 0];
}

function encodeStringField(fieldNumber: number, value: string): number[] {
  const bytes = new TextEncoder().encode(value);
  return [...encodeVarint((fieldNumber << 3) | 2), ...encodeVarint(bytes.length), ...bytes];
}

function encodeMessageField(fieldNumber: number, payload: number[]): Uint8Array {
  return Uint8Array.from([
    ...encodeVarint((fieldNumber << 3) | 2),
    ...encodeVarint(payload.length),
    ...payload,
  ]);
}

function pushBoolField(out: number[], fieldNumber: number, value: boolean | undefined) {
  if (value) {
    out.push(...encodeBoolField(fieldNumber, value));
  }
}

function pushUint32Field(out: number[], fieldNumber: number, value: number | undefined) {
  if (value != null && value !== 0) {
    out.push(...encodeUint32Field(fieldNumber, value));
  }
}

function encodeTrackpadDevice(device: TrackpadDevice): number[] {
  const out: number[] = [];
  pushUint32Field(out, 1, device.id);
  if (device.name) {
    out.push(...encodeStringField(2, device.name));
  }
  pushBoolField(out, 3, device.rotate90);
  pushBoolField(out, 4, device.xInvert);
  pushBoolField(out, 5, device.yInvert);
  pushBoolField(out, 6, device.sleep);
  pushBoolField(out, 7, device.noTaps);
  pushBoolField(out, 8, device.noSecondaryTap);
  pushBoolField(out, 9, device.absoluteGestures);
  pushBoolField(out, 10, device.tapToClick);
  pushBoolField(out, 11, device.doubleTapDrag);
  pushBoolField(out, 12, device.reverseCircularScroll);
  pushUint32Field(out, 13, device.sensitivity);
  pushUint32Field(out, 14, device.xAxisZMin);
  pushUint32Field(out, 15, device.yAxisZMin);
  pushUint32Field(out, 16, device.touchZThreshold);
  pushUint32Field(out, 17, device.xMax);
  pushUint32Field(out, 18, device.yMax);
  pushUint32Field(out, 19, device.edgeScrollMargin);
  pushUint32Field(out, 20, device.pointerDivisor);
  pushUint32Field(out, 21, device.tapTimeoutMs);
  pushUint32Field(out, 22, device.doubleTapMs);
  pushUint32Field(out, 23, device.tapMoveThreshold);
  pushUint32Field(out, 24, device.scrollStep);
  pushBoolField(out, 25, device.ready);
  return out;
}

function encodeTrackpadRequest(request: TrackpadRequest): Uint8Array {
  if (request.listDevices !== undefined) {
    return encodeMessageField(1, []);
  }
  if (request.getDevice !== undefined) {
    return encodeMessageField(2, encodeUint32Field(1, request.getDevice.id));
  }
  if (request.setSleep !== undefined) {
    return encodeMessageField(3, [
      ...encodeUint32Field(1, request.setSleep.id),
      ...encodeBoolField(2, request.setSleep.enabled),
    ]);
  }
  if (request.resetDevice !== undefined) {
    return encodeMessageField(4, encodeUint32Field(1, request.resetDevice.id));
  }
  if (request.setDevice !== undefined) {
    return encodeMessageField(5, Array.from(encodeMessageField(1, encodeTrackpadDevice(request.setDevice.device))));
  }
  throw new Error("Empty trackpad request");
}

const DEMO_DEVICES: TrackpadDevice[] = [
  {
    id: 0,
    name: "demo-left",
    rotate90: true,
    xInvert: false,
    yInvert: false,
    sleep: false,
    noTaps: false,
    noSecondaryTap: false,
    absoluteGestures: true,
    tapToClick: true,
    doubleTapDrag: true,
    reverseCircularScroll: false,
    sensitivity: Sensitivity.SENSITIVITY_2X,
    xAxisZMin: 5,
    yAxisZMin: 4,
    touchZThreshold: 8,
    xMax: 2047,
    yMax: 1535,
    edgeScrollMargin: 220,
    pointerDivisor: 4,
    tapTimeoutMs: 180,
    doubleTapMs: 350,
    tapMoveThreshold: 80,
    scrollStep: 160,
    ready: true,
  },
  {
    id: 1,
    name: "demo-right",
    rotate90: true,
    xInvert: true,
    yInvert: true,
    sleep: false,
    noTaps: false,
    noSecondaryTap: true,
    absoluteGestures: true,
    tapToClick: true,
    doubleTapDrag: false,
    reverseCircularScroll: true,
    sensitivity: Sensitivity.SENSITIVITY_3X,
    xAxisZMin: 6,
    yAxisZMin: 5,
    touchZThreshold: 10,
    xMax: 2047,
    yMax: 1535,
    edgeScrollMargin: 240,
    pointerDivisor: 5,
    tapTimeoutMs: 200,
    doubleTapMs: 300,
    tapMoveThreshold: 70,
    scrollStep: 140,
    ready: true,
  },
];

export function App() {
  const [demoMode, setDemoMode] = useState(false);

  return (
    <div className="app-shell">
      <header className="hero">
        <div>
          <p className="eyebrow">ZMK Studio Custom RPC</p>
          <h1>Cirque Trackpad Control</h1>
        </div>
        <label className="switch">
          <input type="checkbox" checked={demoMode} onChange={(e) => setDemoMode(e.target.checked)} />
          <span>Demo Mode</span>
        </label>
      </header>

      {demoMode ? (
        <>
          <section className="card accent">
            <p>Demo mode is active. No serial connection required.</p>
          </section>
          <TrackpadSection demoMode />
        </>
      ) : (
        <ZMKConnection
          renderDisconnected={({ connect, isLoading, error }) => (
            <section className="card accent">
              {isLoading && <p>Connecting...</p>}
              {error && <p className="error">{error}</p>}
              <div className="row">
                <button
                  className="btn primary"
                  disabled={isLoading}
                  onClick={() => connect(gattConnect)}
                >
                  Connect Bluetooth
                </button>
                <button className="btn" disabled={isLoading} onClick={() => connect(serialConnect)}>
                  Connect Serial
                </button>
              </div>
            </section>
          )}
          renderConnected={({ disconnect, deviceName }) => (
            <>
              <section className="card accent row between">
                <p>Connected: {deviceName}</p>
                <button className="btn" onClick={disconnect}>
                  Disconnect
                </button>
              </section>
              <TrackpadSection demoMode={false} />
            </>
          )}
        />
      )}
    </div>
  );
}

function TrackpadSection({ demoMode }: { demoMode: boolean }) {
  const zmkApp = useContext(ZMKAppContext);
  const [devices, setDevices] = useState<TrackpadDevice[]>([]);
  const [selectedId, setSelectedId] = useState<number | null>(null);
  const [drafts, setDrafts] = useState<Record<number, TrackpadDevice>>({});
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string>("");
  const [hasLoaded, setHasLoaded] = useState(false);
  const connection = zmkApp?.state.connection ?? null;
  const availableSubsystems = ((connection as any)?.subsystems ?? []) as Array<Record<string, unknown>>;
  const availableSubsystemIds = availableSubsystems.map(formatSubsystem);

  const subsystem = useMemo(() => {
    if (!zmkApp || demoMode) {
      return null;
    }
    for (const id of SUBSYSTEM_CANDIDATES) {
      const found = zmkApp.findSubsystem(id);
      if (found) {
        return found;
      }
    }
    return null;
  }, [zmkApp, demoMode]);

  const call = async (request: TrackpadRequest) => {
    if (!zmkApp || !subsystem) {
      throw new Error("Trackpad subsystem not available");
    }
    const conn = zmkApp.state.connection;
    if (!conn) {
      throw new Error("No connection");
    }
    const service = new ZMKCustomSubsystem(conn, subsystem.index);
    const payload = encodeTrackpadRequest(request);
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

  const hydrate = (nextDevices: TrackpadDevice[]) => {
    setDevices(nextDevices);
    setHasLoaded(true);
    setDrafts((prev) => {
      const next = { ...prev };
      for (const d of nextDevices) {
        next[d.id] = d;
      }
      return next;
    });
    if (nextDevices.length > 0 && selectedId == null) {
      setSelectedId(nextDevices[0].id);
    }
  };

  const loadDevices = async () => {
    setBusy(true);
    setError("");
    try {
      if (demoMode) {
        await new Promise((resolve) => setTimeout(resolve, 120));
        hydrate(DEMO_DEVICES);
      } else {
        const resp = await call(Request.create({ listDevices: {} }));
        hydrate(resp.listDevices?.devices ?? []);
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
      if (demoMode) {
        const latest = (drafts[selectedId] ?? devices.find((d) => d.id === selectedId)) as TrackpadDevice | undefined;
        if (!latest) {
          throw new Error("Device not found");
        }
        setDevices((prev) => prev.map((d) => (d.id === selectedId ? { ...latest } : d)));
      } else {
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
        setDrafts((prev) => ({ ...prev, [device.id]: device }));
      }
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
      if (demoMode) {
        setDevices((prev) => prev.map((d) => (d.id === selectedId ? { ...d, sleep: enabled } : d)));
        setDrafts((prev) => ({ ...prev, [selectedId]: { ...prev[selectedId], sleep: enabled } as TrackpadDevice }));
      } else {
        await call(Request.create({ setSleep: { id: selectedId, enabled } }));
        await refreshOne();
      }
    } catch (e) {
      setError(e instanceof Error ? e.message : "RPC failed");
      setBusy(false);
    } finally {
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
      if (demoMode) {
        await new Promise((resolve) => setTimeout(resolve, 120));
        setDevices((prev) => prev.map((d) => (d.id === selectedId ? { ...d, sleep: false } : d)));
      } else {
        await call(Request.create({ resetDevice: { id: selectedId } }));
        await refreshOne();
      }
    } catch (e) {
      setError(e instanceof Error ? e.message : "RPC failed");
      setBusy(false);
    } finally {
      setBusy(false);
    }
  };

  const applySettings = async () => {
    if (selectedId == null || !draft) {
      return;
    }
    setBusy(true);
    setError("");
    try {
      if (demoMode) {
        await new Promise((resolve) => setTimeout(resolve, 120));
        setDevices((prev) => prev.map((d) => (d.id === selectedId ? { ...draft } : d)));
      } else {
        const resp = await call({ setDevice: { device: draft } } as TrackpadRequest);
        const device = resp.getDevice?.device ?? draft;
        setDevices((prev) => prev.map((d) => (d.id === selectedId ? device : d)));
        setDrafts((prev) => ({ ...prev, [selectedId]: device }));
      }
    } catch (e) {
      setError(e instanceof Error ? e.message : "RPC failed");
    } finally {
      setBusy(false);
    }
  };

  const selected = devices.find((d) => d.id === selectedId) ?? null;
  const draft = selectedId != null ? drafts[selectedId] ?? selected : null;
  const canUseRpc = demoMode || Boolean(subsystem);

  const updateDraft = (patch: Partial<TrackpadDevice>) => {
    if (selectedId == null || !draft) {
      return;
    }
    setDrafts((prev) => ({ ...prev, [selectedId]: { ...draft, ...patch } }));
  };

  return (
    <section className="card">
      <div className="section-head">
        <h2>Trackpad Settings</h2>
        {!demoMode && connection && !subsystem && (
          <p className="error">
            Trackpad subsystem not available on current target.
          </p>
        )}
      </div>
      {!demoMode && !connection && <p className="hint">Connect Bluetooth/Serial first.</p>}
      {!demoMode && connection && !subsystem && (
        <p className="hint">
          Tried: {SUBSYSTEM_CANDIDATES.join(", ")} | Available:{" "}
          {availableSubsystemIds.length > 0 ? availableSubsystemIds.join(", ") : "(none)"}
        </p>
      )}
      {!demoMode && subsystem && (
        <p className="hint">
          Using subsystem: {formatSubsystem(subsystem as unknown as Record<string, unknown>)}
        </p>
      )}

      <div className="row">
        <button className="btn primary" disabled={busy || !canUseRpc} onClick={loadDevices}>
          {demoMode ? "Load Demo Devices" : "List Devices"}
        </button>
        <button className="btn" disabled={busy || selectedId == null} onClick={refreshOne}>
          Refresh Selected
        </button>
        <button className="btn" disabled={busy || selectedId == null} onClick={() => setSleep(true)}>
          Sleep ON
        </button>
        <button className="btn" disabled={busy || selectedId == null} onClick={() => setSleep(false)}>
          Sleep OFF
        </button>
        <button className="btn primary" disabled={busy || selectedId == null} onClick={applySettings}>
          Apply Settings
        </button>
        <button className="btn danger" disabled={busy || selectedId == null} onClick={reset}>
          Reset
        </button>
      </div>

      {error && <p className="error">{error}</p>}

      {!demoMode && connection && !subsystem && (
        <p className="hint">
          The connected central does not advertise the trackpad RPC subsystem. Enable
          <code> CONFIG_INPUT_PINNACLE_STUDIO_RPC=y</code> in the JOY firmware to make this UI
          discoverable over Bluetooth.
        </p>
      )}

      {!demoMode && subsystem && hasLoaded && devices.length === 0 && (
        <p className="hint">
          Trackpad RPC is available, but this target reported no local Cirque devices. With JOY as
          central and TPD as peripheral, the UI can only control TPD if JOY exposes or forwards the
          TPD settings through its Studio RPC.
        </p>
      )}

      {devices.length > 0 && (
        <label className="device-select">
          <span>Device</span>
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
        <>
          <p className="hint">
            Edit fields and press <code>Apply Settings</code> to write them to the trackpad.
          </p>
          <div className="grid">
            {BOOL_FIELDS.map((k) => (
              <label key={String(k)} className="field">
                <span>{k}</span>
                <input
                  type="checkbox"
                  checked={Boolean(draft?.[k])}
                  onChange={(e) => updateDraft({ [k]: e.target.checked } as Partial<TrackpadDevice>)}
                />
              </label>
            ))}
            <label className="field">
              <span>sensitivity</span>
              <select
                value={draft?.sensitivity ?? Sensitivity.SENSITIVITY_1X}
                onChange={(e) => updateDraft({ sensitivity: Number(e.target.value) as Sensitivity })}
              >
                <option value={Sensitivity.SENSITIVITY_1X}>1X</option>
                <option value={Sensitivity.SENSITIVITY_2X}>2X</option>
                <option value={Sensitivity.SENSITIVITY_3X}>3X</option>
                <option value={Sensitivity.SENSITIVITY_4X}>4X</option>
              </select>
            </label>
            {NUMBER_FIELDS.map((k) => (
              <label key={String(k)} className="field">
                <span>{k}</span>
                <input
                  type="number"
                  value={Number(draft?.[k] ?? 0)}
                  onChange={(e) => updateDraft({ [k]: Number(e.target.value) } as Partial<TrackpadDevice>)}
                />
              </label>
            ))}
          </div>
          <pre>{JSON.stringify(formatDevice(draft ?? selected), null, 2)}</pre>
        </>
      )}
    </section>
  );
}

function formatSubsystem(subsystem: Record<string, unknown>) {
  const id = subsystem.identifier ?? subsystem.id ?? subsystem.name ?? "(unknown)";
  const index = subsystem.index;
  return index == null ? String(id) : `${String(id)}#${String(index)}`;
}

function formatDevice(d: TrackpadDevice) {
  return {
    ...d,
    sensitivity: Sensitivity[d.sensitivity] ?? d.sensitivity,
  };
}
