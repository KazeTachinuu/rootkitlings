import { RefreshCw } from 'lucide-react'
import { EventBadge } from '../components/EventBadge'

function formatTime(ts) {
  if (!ts) return ''
  const d = new Date(ts.includes('T') ? ts : ts.replace(' ', 'T'))
  if (isNaN(d)) return ts
  const pad = n => String(n).padStart(2, '0')
  return `${pad(d.getMonth()+1)}-${pad(d.getDate())} ${pad(d.getHours())}:${pad(d.getMinutes())}:${pad(d.getSeconds())}`
}

export function Events({ events, onRefresh }) {
  return (
    <div className="p-6 space-y-4">
      <div className="flex items-center justify-between">
        <div>
          <h2 className="text-xl font-bold">Event Log</h2>
          <p className="text-sm text-fg-3">{events.length} event{events.length !== 1 ? 's' : ''}</p>
        </div>
        <button className="nb-btn p-2" onClick={onRefresh} title="Refresh">
          <RefreshCw size={14} />
        </button>
      </div>

      {events.length ? (
        <div className="nb-card-flat overflow-hidden">
          {[...events].reverse().map((e, i) => (
            <div key={i} className="flex items-start gap-3 px-4 py-2.5 border-b-2 border-border last:border-b-0 hover:bg-bg-2 transition-colors">
              <span className="font-mono text-[10px] text-fg-3 shrink-0 pt-0.5 whitespace-nowrap">{formatTime(e.time)}</span>
              <EventBadge type={e.type} />
              <span className="font-mono text-xs text-fg-2 min-w-0 break-all">{e.msg}</span>
            </div>
          ))}
        </div>
      ) : (
        <div className="nb-card p-12 text-center">
          <p className="font-bold text-fg-3">No events yet</p>
          <p className="text-sm text-fg-3 font-mono mt-1">Events appear as targets connect and commands are sent</p>
        </div>
      )}
    </div>
  )
}
