# Allow time for EHCI halt proof

The four-vCPU Freedoom timedemo reproduced an EHCI DMA ownership panic. A
separate trace caught submission being disabled after schedule revocation
failed. The controller eventually reported both schedules stopped and
asserted `HCHalted`, after the driver had exhausted its wait.

Keep the 10 ms limit on the normal asynchronous stop. Give the fallback
controller halt up to 100 ms. DMA ownership still requires observed status.
A stalled halt leaves the transfer buffers live and retains the existing
failure path.

The executable driver fixture models a normal stop, a controller that never
halts, and a controller that halts after 22 ms of total polling. The previous
driver refused the last case after 20 ms. The longer fallback accepts it
after the controller reports halt and the asynchronous schedule goes idle.

All 46 USB tests passed on Windows and WSL. Four-vCPU Freedoom runs reached
graphics initialization and exercised EHCI for 1,500 seconds with the longer
halt wait. A separate run with the empty periodic schedule disabled also
cleared the 1,500-second observation. Full timedemo completion is still open.
