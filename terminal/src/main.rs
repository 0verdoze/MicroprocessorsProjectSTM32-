use std::{time::Duration, sync::Arc};

use egui_number_buffer::NumberBuffer;
use egui_toast::{Toast, Toasts, ToastOptions};
use proto::Frame;
use eframe::{egui::{self, Direction, ComboBox, TextEdit, Response, ScrollArea, Id}, epaint::{ahash::HashMap, Color32, FontId, text::LayoutJob}, emath::Align2};
use serial_com::Cmd;
use tokio::sync::{mpsc::{Sender, UnboundedReceiver, unbounded_channel, UnboundedSender, error::TryRecvError}, oneshot};

mod serial_com;
use serial_com::DeviceHandle;

/// Wrapper around `Frame`, so it can be displayed in the UI
pub struct DrawableFrame {
    inner: Frame,
    /// cached
    crc32: Option<u32>,
    /// cached
    frame_length: Option<usize>,
}

/// shared context between gui and background thread
pub struct Context {
    pub egui_ctx: egui::Context,
    pub runtime: tokio::runtime::Handle,
    pub devices: tokio::sync::Mutex<HashMap<DeviceHandle, Device>>,

    pub cmd_tx: Sender<Cmd>,
    pub error_tx: UnboundedSender<String>,
}

/// represents connected (and selected) device
pub struct Device {
    pub name: String,
    pub cmd_input: String,
    pub handle: DeviceHandle,
    pub received: Vec<DrawableFrame>,
    pub sent: Vec<DrawableFrame>,
}

fn main() -> anyhow::Result<()> {
    // setup logging
    env_logger::init_from_env(env_logger::Env::default().default_filter_or("info"));

    // create tokio runtime (for serial port communication)
    let runtime = create_runtime();

    // basic settings for window
    let options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default()
            .with_inner_size([1280.0, 720.0]),

        ..Default::default()
    };

    // tokio runtime handle, we will pass to closure
    let handle = runtime.handle().clone();    
    eframe::run_native(
        "terminal",
        options,
        Box::new(move |cctx| {
            cctx.egui_ctx.set_pixels_per_point(0.9 as _);
            
            // spsc channel for communication with `serial_com` task
            let (cmd_tx, cmd_rx) = tokio::sync::mpsc::channel(1);
            let (err_tx, err_rx) = unbounded_channel();

            // context shared between UI and COM threads
            let ctx = Arc::new(Context {
                egui_ctx: cctx.egui_ctx.clone(),
                runtime: handle,

                devices: Default::default(),
                cmd_tx,
                error_tx: err_tx,
            });

            // spawn thread for COM communication
            let ctx_cpy = ctx.clone();
            ctx.runtime
                .spawn(async move {
                    serial_com::SerialHandler::new(ctx_cpy, cmd_rx)
                        .run().await
                        .unwrap()
                });

            // UI window
            Box::new(
                App {
                    ctx,
                    new_device_selection: Default::default(),
                    baud_rate: NumberBuffer::new("115200"),

                    toasts: Toasts::new()
                        .direction(Direction::BottomUp)
                        .anchor(Align2::RIGHT_BOTTOM, [-10.0, -10.0]),
                    errors: err_rx,
                }
            )
        })
    ).unwrap();

    // cancel all tasks with 1 second grace window
    runtime.shutdown_timeout(Duration::from_secs(1));
    Ok(())
}

fn create_runtime() -> tokio::runtime::Runtime {
    tokio::runtime::Builder::new_multi_thread()
        .enable_all()
        .build().unwrap()
}

struct App {
    ctx: Arc<Context>,
    new_device_selection: String,
    baud_rate: NumberBuffer<6>,

    toasts: Toasts,
    errors: UnboundedReceiver<String>,
}

impl eframe::App for App {
    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        let devices = tokio_serial::available_ports().unwrap();
        
        // draw main window
        egui::Window::new(format!("{} devices connected", devices.len()))
            .id(egui::Id::new("main window"))
            .show(ctx, |ui| {
                ui.horizontal_top(|ui| {
                    ComboBox::from_id_source("device")
                        .width(ui.available_width() * 0.8)
                        .selected_text(&self.new_device_selection)
                        .show_ui(ui, |ui| {
                            for dev in devices {
                                ui.selectable_value(
                                    &mut self.new_device_selection,
                                    dev.port_name.clone(),
                                    dev.port_name.clone(),
                                );
                            }
                        });

                    ui.text_edit_singleline(&mut self.baud_rate);
                });

                if ui.add_sized([ui.available_width(), 0.0], |ui: &mut egui::Ui| {
                    ui.button("open")
                }).clicked() {
                    let result = self.open_device(
                        self.new_device_selection.clone(),
                        self.baud_rate.get_u64().unwrap_or_default() as u32,
                    );

                    let _ = self.ctx.report_error(result);
                }
            });

        let app_ctx = self.ctx.clone();
        let mut guard = app_ctx.devices.blocking_lock();

        // draw device windows
        guard.retain(|_, device| {
            let mut open = true;

            egui::Window::new(format!("{}", device.name))
                .id(egui::Id::new(device.handle))
                .fixed_size([800.0, 600.0])
                .open(&mut open)
                .show(ctx, |ui| {
                    device.draw(ui, &self.ctx);

                    // ui.allocate_space(ui.available_size());
                });

            if !open {
                self.ctx
                    .cmd_tx
                    .blocking_send(Cmd::CloseDevice {
                        handle: device.handle
                    }).unwrap();
            }

            open
        });

        // push new toast messages
        loop {
            match self.errors.try_recv() {
                Ok(v) => {
                    self.toasts
                        .add(Toast {
                            text: v.into(),
                            kind: egui_toast::ToastKind::Error,
                            options: ToastOptions::default()
                                .show_icon(true)
                                .show_progress(true)
                                .duration_in_seconds(15.0)
                        });
                },
                Err(TryRecvError::Empty) => break,
                Err(TryRecvError::Disconnected) => unreachable!(),
            }
        }

        // show toasts
        self.toasts.show(ctx);
    }
}

impl App {
    // try to open COM device, at `path`, with provided baud_rate
    // on success device will be appended to `self.ctx.device`
    fn open_device(&mut self, path: String, baud_rate: u32) -> anyhow::Result<()> {
        let _guard = self.ctx
            .runtime
            .enter();

        let device = tokio_serial::SerialStream::open(
            &tokio_serial::new(&path, baud_rate)
        )?;

        let (tx, rx) = oneshot::channel();

        self.ctx
            .cmd_tx
            .blocking_send(Cmd::RegisterDevice {
                device, result: tx,
            }).unwrap();

        let handle = rx.blocking_recv().unwrap();
        self.ctx
            .devices
            .blocking_lock()
            .entry(handle)
            .or_insert(Device {
                name: path,
                cmd_input: Default::default(),
                handle,
                received: Default::default(),
                sent: Default::default(),
            });

        Ok(())
    }
}


// ***************************************
// *                 *                   *
// *                 *                   *
// *                 *                   *
// *   SENT FRAMES   *  RECEIVED FRAMES  *
// *                 *                   *
// *                 *                   *
// *                 *                   *
// ***************************************
// *           COMMAND INPUT             *
// ***************************************
// *            SEND BUTTON              *
// ***************************************
/// draw device window
impl Device {
    fn draw(&mut self, ui: &mut egui::Ui, ctx: &Arc<Context>) {
        ui.style_mut().wrap = Some(false);

        ui.horizontal_top(|ui: &mut egui::Ui| {
            let space = ui.available_width() / 2.0 - 1.0;

            ui.vertical(|ui| {
                ScrollArea::new([false, true])
                    .id_source(Id::new("left").with(ui.id()))
                    .show(ui, |ui| {
                        self.sent
                            .iter()
                            .for_each(|frame| {
                                frame.draw(ui, space);
                            });
                    });

                ui.allocate_space([space, 0.0].into());
            });

            ui.add_sized([0.0, ui.available_height() - 30.0], |ui: &mut egui::Ui| {
                ui.add(egui::Separator::default())
            });

            ui.vertical_centered(|ui| {
                let space = ui.available_width();

                ScrollArea::new([false, true])
                    .id_source(Id::new("right").with(ui.id()))
                    .show(ui, |ui| {
                        self.received
                            .iter()
                            .for_each(|frame| {
                                frame.draw(ui, space);
                            });
                    });
            });

            // ui.vertical();

            ()
        });

        ui.horizontal_top(|ui: &mut egui::Ui| {
            ui.add(TextEdit::singleline(&mut self.cmd_input).desired_width(ui.available_width() * 0.8));
            
            if ui.add_sized([ui.available_width(), 0.0], |ui: &mut egui::Ui| ui.button("Send")).clicked() {
                let frame = Frame {
                    sender: 123,
                    receiver: 100,
                    data: self.cmd_input.clone().into_bytes(),
                };
                self.cmd_input.clear();

                if let Some(data) = ctx.report_error((|| anyhow::Ok(frame.serialize()?))()) {
                    let (result_tx, result) = oneshot::channel();
                    ctx.cmd_tx
                        .blocking_send(Cmd::SendData { handle: self.handle, data, result: result_tx })
                        .unwrap();

                    if let Some(_) = ctx.report_error(result.blocking_recv().unwrap()) {
                        self.sent.push(frame.into());
                    }
                }

            }
        });
    }
}

impl Context {
    #[must_use]
    pub fn report_error<T>(&self, result: anyhow::Result<T>) -> Option<T> {
        match result {
            Ok(v) => Some(v),
            Err(err) => {
                self.error_tx
                    .send(format!("{:?}", err))
                    .unwrap();

                None
            }
        }
    }
}

impl DrawableFrame {
    fn draw(&self, ui: &mut egui::Ui, aval: f32) -> Response {
        let free_chars = (aval / 9.0) as usize;

        let crc32 = Self::format_crc32(self.crc32);
        let len = Self::format_length(self.frame_length);

        let cmd = Self::format_name(&String::from_utf8_lossy(&self.inner.data), free_chars.saturating_sub(6));

        let layout = LayoutJob::simple(
            format!(
                "[CMD] {}\nR:{:0<3} S:{:0<3} CRC32:{crc32} LEN:{len}",
                cmd,
                self.inner.receiver,
                self.inner.sender,
            ),
            FontId::monospace(14.0),
            Color32::GRAY,
            aval,
        );

        let resp = ui.add_sized([aval, 0.0],
            egui::SelectableLabel::new(
                false,
                layout,
            )
        );

        if resp.secondary_clicked() {
            // copy hex to keyboard
            let serialized = self.inner.serialize().unwrap();
            let hex = serialized.iter()
                .map(|c| format!("{:02x}", c))
                .collect::<Vec<_>>()
                .join("");

            let mut clipboard = arboard::Clipboard::new().unwrap();
            clipboard.set_text(&hex).unwrap()
        }

        resp
    }

    fn format_name(name: &str, space: usize) -> String {
        let space = space.max(3);

        let len = name.chars().count();
        if len > space {
            let (pos,_) = name.char_indices().skip(space-2).next().unwrap();
            format!("{:.<space$}", &name[..pos])
        } else {
            format!("{: <space$}", name)
        }
    }

    fn format_crc32(crc: Option<u32>) -> String {
        if let Some(n) = crc {
            format!("{:0>8x}", n)
        } else {
            format!("{: >8}", "")
        }
    }

    fn format_length(len: Option<usize>) -> String {
        if let Some(n) = len {
            format!("{: <4}", n)
        } else {
            format!("{: <4}", "")
        }
    }
}

impl From<Frame> for DrawableFrame {
    fn from(value: Frame) -> Self {
        let crc32 = value.calculate_crc32()
            .ok();

        let frame_length = value.serialize()
            .map(|v| v.len())
            .ok();

        Self {
            inner: value,
            crc32,
            frame_length,
        }
    }
}
