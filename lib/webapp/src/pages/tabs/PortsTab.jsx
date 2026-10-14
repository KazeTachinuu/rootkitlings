import { useState, useCallback } from 'react'
import { Network, Eye, EyeOff, Radio } from 'lucide-react'
import { Th } from '../../components/Th'
import { useSnapshot, SnapshotBar, EmptySnapshotCta, HiddenSummary } from '../Target'

const PORTS_CMD = "ss -tulnp 2>/dev/null || netstat -tulnp 2>/dev/null || cat /proc/net/tcp /proc/net/tcp6 2>/dev/null"

export function PortsTab({ target: t, cmd, execCmd, busy }) {
  const snap = useSnapshot(`ports:${t.id}`)
  const hiddenPorts = t.hidden?.port || []

  const capture = useCallback(() => {
    snap.setLoading(true)
    execCmd(PORTS_CMD, t.id).then(r => {
      if (r.ok) {
        const lines = r.output.split('\n').filter(Boolean)
        let parsed
        const ssIdx = lines.findIndex(l => l.trim().startsWith('Netid'))
        const netstatIdx = lines.findIndex(l => l.trim().startsWith('Proto'))
        if (ssIdx >= 0) {
          parsed = lines.slice(ssIdx + 1).map(line => {
            const parts = line.trim().split(/\s+/)
            if (parts.length < 5) return null
            const proto = parts[0]
            const state = parts[1]
            const local = parts[4] || ''
            const foreign = parts[5] || ''
            const portMatch = local.match(/:(\d+)$/)
            const port = portMatch ? portMatch[1] : ''
            const procMatch = line.match(/users:\(\("([^"]+)"/)
            return { proto, local, foreign, port, state, proc: procMatch ? procMatch[1] : '-' }
          }).filter(Boolean)
        } else if (netstatIdx >= 0) {
          parsed = lines.slice(netstatIdx + 1).map(line => {
            const parts = line.trim().split(/\s+/)
            if (parts.length < 6) return null
            const local = parts[3] || ''
            const foreign = parts[4] || ''
            const portMatch = local.match(/:(\d+)$/)
            const port = portMatch ? portMatch[1] : ''
            const proc = parts.length >= 7 ? parts[6] : '-'
            return { proto: parts[0], local, foreign, port, state: parts[5], proc }
          }).filter(Boolean)
        } else {
          const TCP_STATES = { '01': 'ESTABLISHED', '02': 'SYN_SENT', '06': 'TIME_WAIT', '0A': 'LISTEN' }
          parsed = lines.filter(l => l.trim() && !l.trim().startsWith('sl')).map(line => {
            const parts = line.trim().split(/\s+/)
            if (parts.length < 4) return null
            const localHex = parts[1] || ''
            const foreignHex = parts[2] || ''
            const parseAddr = (h) => {
              const [aH, pH] = h.split(':')
              if (!pH) return { addr: '?', port: '0' }
              const port = parseInt(pH, 16).toString()
              if (aH.length === 8) {
                const a = parseInt(aH, 16)
                return { addr: `${a & 0xff}.${(a >> 8) & 0xff}.${(a >> 16) & 0xff}.${(a >> 24) & 0xff}`, port }
              }
              if (aH === '00000000000000000000000000000000') return { addr: '::', port }
              if (aH.startsWith('0000000000000000FFFF0000')) {
                const a = parseInt(aH.slice(24), 16)
                return { addr: `${a & 0xff}.${(a >> 8) & 0xff}.${(a >> 16) & 0xff}.${(a >> 24) & 0xff}`, port }
              }
              return { addr: '[' + aH.match(/.{8}/g).map(g => {
                const n = parseInt(g, 16)
                return ((n & 0xff) << 8 | (n >> 8) & 0xff).toString(16)
              }).join(':') + ']', port }
            }
            const loc = parseAddr(localHex)
            const rem = parseAddr(foreignHex)
            const state = TCP_STATES[parts[3]] || parts[3]
            return { proto: 'tcp', local: `${loc.addr}:${loc.port}`, foreign: `${rem.addr}:${rem.port}`, port: loc.port, state, proc: '-' }
          }).filter(Boolean)
        }
        snap.set(parsed)
      } else {
        snap.fail(r.output || 'failed')
      }
    })
  }, [execCmd, t.id, snap])

  const [panel, setPanel] = useState('scan')
  const ports = snap.data || []

  if (!snap.time && !snap.loading)
    return <EmptySnapshotCta icon={Network} title="TCP Connections"
      subtitle="Scan for TCP connections and listeners on the target"
      onCapture={capture} online={t.online && !busy} loading={snap.loading} />

  return (
    <div className="flex flex-col h-full">
      <SnapshotBar title="Listening Ports" icon={Network} count={ports.length} hiddenCount={hiddenPorts.length}
        time={snap.time} loading={snap.loading} onSnapshot={capture} online={t.online && !busy}>
        <div className="flex border-2 border-border rounded-[var(--radius)] overflow-hidden shrink-0 ml-2">
          <button
            className={`px-3 py-1 text-sm font-bold ${panel === 'scan' ? 'bg-main text-main-fg' : 'hover:bg-bg'}`}
            onClick={() => setPanel('scan')}
          >Scan</button>
          <button
            className={`px-3 py-1 text-sm font-bold border-l-2 border-border ${panel === 'hidden' ? 'bg-main text-main-fg' : 'hover:bg-bg'}`}
            onClick={() => setPanel('hidden')}
          >Hidden {hiddenPorts.length}</button>
        </div>
      </SnapshotBar>
      {panel === 'scan' ? (
        <>
          {snap.error && (
            <div className="p-6 text-center">
              <div className="text-red font-bold mb-1">Scan failed</div>
              <div className="text-sm text-fg-3">{snap.error}</div>
            </div>
          )}
          {!snap.error && !ports.length && !snap.loading ? (
            <div className="flex-1 flex items-center justify-center text-fg-3 text-sm">
              No listening ports found. Hit Scan to refresh.
            </div>
          ) : !snap.error && ports.length > 0 && (
          <div className="flex-1 overflow-auto">
            <table className="w-full text-sm">
              <thead className="sticky top-0 bg-secondary border-b-2 border-border">
                <tr><Th>Proto</Th><Th>Local Address</Th><Th>Foreign Address</Th><Th>Port</Th><Th>State</Th><Th>Process</Th><Th></Th></tr>
              </thead>
              <tbody>
                {ports.map((p, i) => {
                  const isHidden = hiddenPorts.includes(p.port)
                  return (
                    <tr key={i} className={`border-b border-border hover:bg-bg-2 group ${isHidden ? 'opacity-40' : ''}`}>
                      <td className="px-4 py-2 font-mono text-fg-3">{p.proto}</td>
                      <td className="px-4 py-2 font-mono">{p.local}</td>
                      <td className="px-4 py-2 font-mono text-fg-3">{p.foreign || '-'}</td>
                      <td className="px-4 py-2 font-mono font-bold">{p.port}</td>
                      <td className="px-4 py-2">
                        <span className={`nb-badge text-[9px] py-0 ${p.state === 'LISTEN' ? 'bg-green text-white' : p.state === 'ESTABLISHED' ? 'bg-blue text-white' : 'bg-bg-2'}`}>
                          {p.state}
                        </span>
                      </td>
                      <td className="px-4 py-2 font-mono text-xs text-fg-3">{p.proc || '-'}</td>
                      <td className="px-4 py-2 text-right">
                        <button
                          className={`p-1 transition-opacity ${isHidden ? 'text-green opacity-100' : 'text-fg-3 hover:text-red opacity-0 group-hover:opacity-100'}`}
                          title={isHidden ? 'Unhide port' : 'Hide port'}
                          onClick={() => {
                            if (isHidden) {
                              const idx = hiddenPorts.indexOf(p.port)
                              if (idx >= 0) cmd(`UNHIDE PORT ${idx}`)
                            } else {
                              cmd(`HIDE PORT ${p.port}`)
                            }
                          }}
                        >
                          {isHidden ? <Eye size={14} /> : <EyeOff size={14} />}
                        </button>
                      </td>
                    </tr>
                  )
                })}
              </tbody>
            </table>
          </div>
          )}
        </>
      ) : (
        <div className="flex-1 overflow-auto">
          <HiddenSummary title="Hidden Ports" icon={Network} items={hiddenPorts}
            onRemove={i => cmd(`UNHIDE PORT ${i}`)} onAdd={v => cmd(`HIDE PORT ${v}`)} placeholder="4444" />
        </div>
      )}
    </div>
  )
}
