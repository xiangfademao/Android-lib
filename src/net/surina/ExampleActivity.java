/////////////////////////////////////////////////////////////////////////////
///
/// Example Android Application/Activity that allows processing WAV 
/// audio files with SoundTouch library
///
/// Copyright (c) Olli Parviainen
///
////////////////////////////////////////////////////////////////////////////////

package net.surina;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.IOException;

import net.surina.soundtouch.PcmToWavUtil;
import net.surina.soundtouch.SoundTouch;
import net.surina.soundtouchexample.R;

import android.app.Activity;
import android.content.Intent;
import android.media.AudioFormat;
import android.net.Uri;
import android.os.AsyncTask;
import android.os.Bundle;
import android.os.Environment;
import android.util.Log;
import android.view.View;
import android.view.View.OnClickListener;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.TextView;
import android.widget.Toast;

import static android.media.AudioFormat.ENCODING_PCM_16BIT;

public class ExampleActivity extends Activity implements OnClickListener {
    TextView textViewConsole = null;
    EditText editSourceFile = null;
    EditText editOutputFile = null;
    EditText editTempo = null;
    EditText editPitch = null;
    CheckBox checkBoxPlay = null;

    StringBuilder consoleText = new StringBuilder();
    File soundTouchTemp;
    File soundTouchTempWav;
    FileOutputStream fileOutputStream;

    /// Called when the activity is created
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_example);
        soundTouchTemp = new File(Environment.getExternalStorageDirectory(), "soundTouchTemp");
        soundTouchTempWav = new File(Environment.getExternalStorageDirectory(), "soundTouchTempWav.wav");
        if (soundTouchTemp.exists()) {
            soundTouchTemp.delete();
        }
        try {
            soundTouchTemp.createNewFile();
        } catch (IOException e) {
            e.printStackTrace();
        }
        if (soundTouchTempWav.exists()) {
            soundTouchTempWav.delete();
        }
        try {
            soundTouchTempWav.createNewFile();
        } catch (IOException e) {
            e.printStackTrace();
        }
        try {
            fileOutputStream = new FileOutputStream(soundTouchTemp);
        } catch (FileNotFoundException e) {
            e.printStackTrace();
        }
        textViewConsole = (TextView) findViewById(R.id.textViewResult);
        editSourceFile = (EditText) findViewById(R.id.editTextSrcFileName);
        editOutputFile = (EditText) findViewById(R.id.editTextOutFileName);

        editTempo = (EditText) findViewById(R.id.editTextTempo);
        editPitch = (EditText) findViewById(R.id.editTextPitch);

        Button buttonFileSrc = (Button) findViewById(R.id.buttonSelectSrcFile);
        Button buttonFileOutput = (Button) findViewById(R.id.buttonSelectOutFile);
        Button buttonProcess = (Button) findViewById(R.id.buttonProcess);
        buttonFileSrc.setOnClickListener(this);
        buttonFileOutput.setOnClickListener(this);
        buttonProcess.setOnClickListener(this);

        checkBoxPlay = (CheckBox) findViewById(R.id.checkBoxPlay);

        // Check soundtouch library presence & version
        checkLibVersion();
    }


    /// Function to append status text onto "console box" on the Activity
    public void appendToConsole(final String text) {
        // run on UI thread to avoid conflicts
        runOnUiThread(new Runnable() {
            public void run() {
                consoleText.append(text);
                consoleText.append("\n");
                textViewConsole.setText(consoleText);
            }
        });
    }


    /// print SoundTouch native library version onto console
    protected void checkLibVersion() {
        String ver = SoundTouch.getVersionString();
        appendToConsole("SoundTouch native library version = " + ver);
    }


    /// Button click handler
    @Override
    public void onClick(View arg0) {
        switch (arg0.getId()) {
            case R.id.buttonSelectSrcFile:
            case R.id.buttonSelectOutFile:
                // one of the file select buttons clicked ... we've not just implemented them ;-)
                Toast.makeText(this, "File selector not implemented, sorry! Enter the file path manually ;-)", Toast.LENGTH_LONG).show();
                break;

            case R.id.buttonProcess:
                // button "process" pushed
                process();
                break;
        }

    }


    /// Play audio file
    protected void playWavFile(String fileName) {
        File file2play = new File(fileName);
        Intent i = new Intent();
        i.setAction(android.content.Intent.ACTION_VIEW);
        i.setDataAndType(Uri.fromFile(file2play), "audio/wav");
        startActivity(i);
    }


    /// Helper class that will execute the SoundTouch processing. As the processing may take
    /// some time, run it in background thread to avoid hanging of the UI.
    protected class ProcessTask extends AsyncTask<ProcessTask.Parameters, Integer, Long> implements SoundTouch.ReceiveCallBack {

        @Override
        public void onReceive(byte[] buffer) {
            try {
                fileOutputStream.write(buffer);
            } catch (IOException e) {
                e.printStackTrace();
            }
        }

        /// Helper class to store the SoundTouch file processing parameters
        public final class Parameters {
            String inFileName;
            String outFileName;
            float tempo;
            float pitch;
        }


        /// Function that does the SoundTouch processing
        public final long doSoundTouchProcessing(Parameters params) {

            SoundTouch st = new SoundTouch();
            st.setReceiveCallBack(this);
            st.setTempo(params.tempo);
            st.setPitchSemiTones(params.pitch);
            long startTime = System.currentTimeMillis();


            byte[] buffer = new byte[4096 * 2];
            FileInputStream fileInputStream = null;
            try {
                fileInputStream = new FileInputStream(params.inFileName);
                fileInputStream.skip(44);
                Log.i("SoundTouch", "process file " + params.inFileName + " fileSize=" + fileInputStream.available());
                int readSize;
                while (true) {
                    readSize = fileInputStream.read(buffer);
                    if (readSize == -1) {
                        break;
                    }
                    Log.e("SoundTouch", "doSoundTouchProcessing buffer=" + buffer.length + " readSize=" + readSize);
                    if (buffer.length > readSize) {
                        byte[] subBytes = new byte[readSize];
                        System.arraycopy(buffer, 0, subBytes, 0, readSize);
                        buffer = subBytes;
                    }
                    st.processFrame(buffer, 16000, 1);
                }
                st.processFrame(null, 16000, 1);
                new PcmToWavUtil(16000, AudioFormat.CHANNEL_IN_MONO, ENCODING_PCM_16BIT).
                        pcmToWav(soundTouchTemp.getPath(), soundTouchTempWav.getPath());
            } catch (FileNotFoundException e) {
                e.printStackTrace();
            } catch (IOException e) {
                e.printStackTrace();
            }
            if (fileInputStream != null) {
                try {
                    fileInputStream.close();
                } catch (IOException e) {
                    e.printStackTrace();
                }
            }


            int res = st.processFile(params.inFileName, params.outFileName);
            long endTime = System.currentTimeMillis();
            float duration = (endTime - startTime) * 0.001f;

            try {
                Log.i("SoundTouch", "process file done, duration = " + duration + " outPutFileSize=" + new FileInputStream(new File(params.outFileName)).available());
            } catch (IOException e) {
                e.printStackTrace();
            }
            appendToConsole("Processing done, duration " + duration + " sec.");
            if (res != 0) {
                String err = SoundTouch.getErrorString();
                appendToConsole("Failure: " + err);
                return -1L;
            }


            // Play file if so is desirable
            if (checkBoxPlay.isChecked()) {
                playWavFile(params.outFileName);
            }
            return 0L;
        }


        /// Overloaded function that get called by the system to perform the background processing
        @Override
        protected Long doInBackground(Parameters... aparams) {
            return doSoundTouchProcessing(aparams[0]);
        }

    }


    /// process a file with SoundTouch. Do the processing using a background processing
    /// task to avoid hanging of the UI
    protected void process() {
        try {
            ProcessTask task = new ProcessTask();
            ProcessTask.Parameters params = task.new Parameters();
            // parse processing parameters
            params.inFileName = editSourceFile.getText().toString();
            params.outFileName = editOutputFile.getText().toString();
            params.tempo = 0.01f * Float.parseFloat(editTempo.getText().toString());
            params.pitch = Float.parseFloat(editPitch.getText().toString());

            // update UI about status
            appendToConsole("Process audio file :" + params.inFileName + " => " + params.outFileName);
            appendToConsole("Tempo = " + params.tempo);
            appendToConsole("Pitch adjust = " + params.pitch);

            Toast.makeText(this, "Starting to process file " + params.inFileName + "...", Toast.LENGTH_SHORT).show();

            // start SoundTouch processing in a background thread
            task.execute(params);
//			task.doSoundTouchProcessing(params);	// this would run processing in main thread

        } catch (Exception exp) {
            exp.printStackTrace();
        }

    }

    public static float[] byteToIntArr(byte[] bytes, int max) {
        if (max % 4 != 0) {
            return null;
        }
        float[] intarr = new float[max / 4];

        int i1, i2, i3, i4;
        for (int j = 0, k = 0; j < intarr.length; j++, k += 4)//j循环int		k循环byte数组
        {
            i1 = bytes[k];
            i2 = bytes[k + 1];
            i3 = bytes[k + 2];
            i4 = bytes[k + 3];

            if (i1 < 0) {
                i1 += 256;
            }
            if (i2 < 0) {
                i2 += 256;
            }
            if (i3 < 0) {
                i3 += 256;
            }
            if (i4 < 0) {
                i4 += 256;
            }
            intarr[j] = (i1 << 24) + (i2 << 16) + (i3 << 8) + (i4 << 0);//保存Int数据类型转换

        }
        return intarr;
    }
}