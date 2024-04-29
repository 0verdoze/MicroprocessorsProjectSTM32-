

// pub fn start_runtime() -> anyhow::Result<()> {
//     println!("{:?}", ?);

//     Ok(())
// }

use std::{sync::{Arc, atomic::{AtomicU64, Ordering}}, collections::HashMap};

use proto::Frame;
use tokio::sync::mpsc::{Receiver, unbounded_channel, UnboundedSender, UnboundedReceiver};
use tokio::sync::oneshot;
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio_serial::SerialStream;
use tokio_util::sync::CancellationToken;

use crate::{Context, DrawableFrame};

static HANDLE_COUNTER: AtomicU64 = AtomicU64::new(0);
pub struct SerialHandler {
    ctx: Arc<Context>,
    cmd_rx: Receiver<Cmd>,
    
    devices: HashMap<DeviceHandle, DeviceThread>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct DeviceHandle(u64);

pub enum Cmd {
    RegisterDevice {
        device: SerialStream,
        result: oneshot::Sender<DeviceHandle>,
    },
    CloseDevice {
        handle: DeviceHandle,
    },
    SendData {
        handle: DeviceHandle,
        data: Vec<u8>,
        result: oneshot::Sender<anyhow::Result<()>>,
    },
}

struct DeviceThread {
    cancel_token: CancellationToken,
    tx: UnboundedSender<(Vec<u8>, oneshot::Sender<anyhow::Result<()>>)>,
}

struct FrameBuilder {
    buf: Vec<u8>,
}

impl SerialHandler {
    pub fn new(ctx: Arc<Context>, cmd_rx: Receiver<Cmd>) -> Self {
        Self {
            ctx,
            cmd_rx,
            devices: Default::default(),
        }
    }

    pub async fn run(&mut self) -> anyhow::Result<()> {
        while let Some(cmd) = self.cmd_rx.recv().await {
            match cmd {
                Cmd::RegisterDevice { device, result } => {
                    let handle = DeviceHandle(
                        HANDLE_COUNTER.fetch_add(1, Ordering::Relaxed)
                    );
                    
                    let (tx, rx) = unbounded_channel();
                    let cancel_token = CancellationToken::new();
                    tokio::spawn(Self::device_handler(
                        self.ctx.clone(),
                        cancel_token.clone(),
                        handle,
                        device,
                        rx,
                    ));

                    if result.send(handle).is_ok() {
                        self.devices
                            .entry(handle)
                            .or_insert(DeviceThread {
                                cancel_token,
                                tx,
                            });
                    }
                },
                Cmd::CloseDevice { handle } => {
                    self.devices
                        .remove(&handle)
                        .map(|v| v.cancel_token.cancel());
                },
                Cmd::SendData { handle, data, result } => {
                    if let Some(v) = self.devices.get(&handle) {
                        if let Err(err) = v.tx.send((data, result)) {
                            let _ = err.0.1.send(Err(
                                anyhow::anyhow!("unable to send data to worker thread, channel closed")
                            ));
                        }
                    } else {
                        let _ = result.send(Err(
                            anyhow::anyhow!("invalid handle")
                        ));
                    }
                }
            }
        }

        Ok(())
    }

    async fn device_handler(
        ctx: Arc<Context>,
        cancel: CancellationToken,
        handle: DeviceHandle,
        device: SerialStream,
        mut rx: UnboundedReceiver<(Vec<u8>, oneshot::Sender<anyhow::Result<()>>)>,
    ) {
        let mut rx_buffer = vec![0u8; 128];
        let mut frame_builder = FrameBuilder::new();

        let (mut recv, mut send) = tokio::io::split(device);

        loop {
            tokio::select! {
                biased;

                _ = cancel.cancelled() => { return; },

                option = rx.recv() => {
                    if let Some((data, r)) = option {
                        log::info!("SENDING FRAME: {}", display_bytes::display_bytes(&data));
                        let result = send.write_all(&data).await;

                        let _ = r.send((move || -> anyhow::Result<()> { result?; Ok(()) })());
                    } else {
                        // inform about error?
                        cancel.cancel()
                    }
                }

                result = recv.read(&mut rx_buffer) => {
                    match result {
                        Ok(read) => {
                            // println!("recv {}", display_bytes::display_bytes(&rx_buffer[..read]));
                            let frames = frame_builder.push_buf(&rx_buffer[..read]);

                            let mut devices = ctx.devices
                                .lock().await;

                            if let Some(dev) = devices.get_mut(&handle) {
                                dev.received
                                    .extend(frames.into_iter().map(|frame| DrawableFrame::from(frame)));

                                ctx.egui_ctx
                                    .request_repaint();
                            } else {
                                // unable to find self ...
                                cancel.cancel()
                            }
                        },
                        Err(err) => {
                            log::warn!("{:?}", err);
                            cancel.cancel()
                        }
                    }
                }
            }
        }
    }
}

impl FrameBuilder {
    fn new() -> Self {
        Self {
            buf: Vec::with_capacity(1512),
        }
    }

    fn push_buf(&mut self, buf: &[u8]) -> Vec<Frame> {
        let mut out = Vec::new();

        for b in buf {
            if let Some(frame) = self.push_byte(*b) {
                out.push(frame);
            }
        }
        
        // if !out.is_empty() {
        //     println!("new frame");
        // }

        out
    }

    fn push_byte(&mut self, byte: u8) -> Option<Frame> {
        const FRAME_MAX_LEN: usize = 1280;

        match byte {
            Frame::BEGIN_FRAME_BYTE => {
                self.buf.clear();
                self.buf.push(byte);

                None
            },
            Frame::END_FRAME_BYTE => {
                if !self.buf.is_empty() {
                    self.buf.push(byte);

                    let result = Frame::deserialize(&self.buf);
                    self.buf.clear();

                    if let Err(err) = result.as_ref() {
                        log::info!("discarded frame, reason `{}`", err);
                    }
                    result.ok()
                } else {
                    None
                }
            },
            _ => {
                if !self.buf.is_empty() {
                    self.buf.push(byte);
                }

                if self.buf.len() == FRAME_MAX_LEN {
                    self.buf.clear();
                }

                None
            }
        }
    }
}
