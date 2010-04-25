﻿using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Documents;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Navigation;
using System.Windows.Shapes;
using System.Runtime.InteropServices;

namespace spectrum
{

    /// <summary>
    /// Interaction logic for MainWindow.xaml
    /// </summary>
    public partial class MainWindow : Window
    {
        public MainWindow()
        {
            InitializeComponent();

            var result = FMOD.Factory.System_Create(ref system);
            FmodCheck(result);

            uint version = 0;
            result = system.getVersion(ref version);
            FmodCheck(result);
            if (version < FMOD.VERSION.number) {
                MessageBox.Show("Error!  You are using an old version of FMOD " + version.ToString("X") + ".  This program requires " + FMOD.VERSION.number.ToString("X") + ".");
                //Application.Exit();
            }

            result = system.init(32, FMOD.INITFLAGS.NORMAL, (IntPtr)null);
            FmodCheck(result);
        }

        private void FmodCheck(FMOD.RESULT result)
        {
            if (result != FMOD.RESULT.OK) {
                //timer.Stop();
                MessageBox.Show("FMOD error! " + result + " - " + FMOD.Error.String(result));
                Environment.Exit(-1);
            }
        }

        private void FileOpen_Click(object sender, RoutedEventArgs e)
        {
            var dlg = new Microsoft.Win32.OpenFileDialog();
            dlg.DefaultExt = "mp3";
            if (dlg.ShowDialog() == null) {
                return;
            }

            var filename = dlg.FileName;
            var result = system.createSound(filename, FMOD.MODE.SOFTWARE | FMOD.MODE._2D, ref sound);
            //var result = system.createStream(filename, FMOD.MODE.SOFTWARE | FMOD.MODE._2D, ref sound);
            FmodCheck(result);

            uint totalBytes = 0;
            sound.getLength(ref totalBytes, FMOD.TIMEUNIT.PCMBYTES);
            uint lengthInMs = 0;
            sound.getLength(ref lengthInMs, FMOD.TIMEUNIT.MS);
            FMOD.SOUND_TYPE type = FMOD.SOUND_TYPE.UNKNOWN;
            FMOD.SOUND_FORMAT format = FMOD.SOUND_FORMAT.NONE;
            int channels = 0;
            int bits = 0;
            sound.getFormat(ref type, ref format, ref channels, ref bits);

            IntPtr ptr1 = new IntPtr();
            IntPtr ptr2 = new IntPtr();
            uint len1 = 0;
            uint len2 = 0;
            var res = sound.@lock(0, totalBytes, ref ptr1, ref ptr2, ref len1, ref len2);
            FmodCheck(res);
            var numSamples = totalBytes / (bits / 8 * channels);
            var sampleSize = bits / 8 * channels;
            var cur = 0;
            byte [] tmp = new byte[len1];
            Marshal.Copy(ptr1, tmp, 0, (int)len1);
            for (var i = 0; i < numSamples; ++i) {
                float l = (float)BitConverter.ToInt16(tmp, cur) / 65536;
                float r = (float)BitConverter.ToInt16(tmp, cur + 2) / 65536;
                canvas1.left_amp.Add(l);
                canvas1.right_amp.Add(r);
                cur += sampleSize;
            }
            sound.unlock(ptr1, ptr2, len1, len2);
            result = system.playSound(FMOD.CHANNELINDEX.FREE, sound, false, ref channel);
            canvas1.channel = channel;
            canvas1.sound = sound;

            canvas1.InvalidateVisual();

            timer.Tick += new EventHandler(timer_Tick);
            timer.Interval = TimeSpan.FromMilliseconds(100);
            timer.Start();
        }

        void timer_Tick(object sender, EventArgs e)
        {
            uint pos = 0;
            uint len = 0;
            channel.getPosition(ref pos, FMOD.TIMEUNIT.MS);
            sound.getLength(ref len, FMOD.TIMEUNIT.MS);
            canvas1.SongPos = pos / (float)len;
            canvas1.InvalidateVisual();
        }

        private System.Windows.Threading.DispatcherTimer timer = new System.Windows.Threading.DispatcherTimer();

        private FMOD.System system = null;
        private FMOD.Sound sound = null;
        private FMOD.Channel channel = null;

        private void Redraw_Click(object sender, RoutedEventArgs e)
        {
            canvas1.InvalidateVisual();
        }

        private void slider1_ValueChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
        {
            var v = e.NewValue;
        }


    }

    public class MyCanvas : Canvas
    {
        protected override void OnRender(DrawingContext dc)
        {

            if (left_amp.Count == 0 || sound == null || channel == null) {
                return;
            }
/*
            if (left_amp.Count < ActualWidth) {
                return;
            }

            var prev = new Point(0, Height - Height * left_amp[0]);
            var black_pen = new Pen(Brushes.Black, 1);
            for (int i = 1; i < ActualWidth; ++i) {
                int idx = (int)((i / (float)ActualWidth) * left_amp.Count());
                var cur = new Point(i, Height - Height * left_amp[idx]);
                dc.DrawLine(black_pen, prev, cur);
                prev = cur;
            }
*/
            uint pos = 0;
            channel.getPosition(ref pos, FMOD.TIMEUNIT.PCMBYTES);

            uint len = 0;
            sound.getLength(ref len, FMOD.TIMEUNIT.PCMBYTES);

            long tmp = pos * left_amp.Count / len;
            int start_idx = (int)(tmp);

            var prev = new Point(0, Height - Height * left_amp[start_idx]);
            var black_pen = new Pen(Brushes.Black, 1);
            var j = 0;
            for (int i = start_idx + 1; i < left_amp.Count && j < ActualWidth; ++i) {
                var cur = new Point(j, Height - Height * left_amp[i]);
                j += 10;
                dc.DrawLine(black_pen, prev, cur);
                prev = cur;
            }


            var cur_pos = ActualWidth * SongPos;
            var red_pen = new Pen(Brushes.Red, 1);
            dc.DrawLine(red_pen, new Point(cur_pos, 0), new Point(cur_pos, Height));
        }

        public float SongPos { get; set; }
        public List<float> left_amp = new List<float>();
        public List<float> right_amp = new List<float>();
        public FMOD.Sound sound = null;
        public FMOD.Channel channel = null;
    }

}