/*
    Copyright (C) 2011 Mario Stephan <mstephan@shared-files.de>

    This library is free software; you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published
    by the Free Software Foundation; either version 2.1 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "trackanalyser.h"

#include <QtGui>
#if QT_VERSION >= 0x050000
 #include <QtConcurrent/QtConcurrent>
#else
 #include <QtConcurrentRun>
#endif

#define AUDIOFREQ 32000
#define SCAN_DURATION 60
static const guint spect_bands = 8;

struct TrackAnalyser_Private
{
        QFutureWatcher<void> watcher;
        QMutex mutex;
        guint64 fft_res;
        float lastSpectrum[spect_bands];
        QList<float> spectralFlux;
        int bpm;
        GstElement *src, *conv, *sink, *cutter, *audio, *analysis, *spectrum;
        TrackAnalyser::modeType analysisMode;
};

TrackAnalyser::TrackAnalyser(QWidget *parent) :
        QWidget(parent),
    pipeline(nullptr), m_finished(false)
    , p( new TrackAnalyser_Private )
{
    p->fft_res = 435; //sample rate for fft samples in Hz
    for (int i=0;i<spect_bands;i++)
        p->lastSpectrum[i]=0.0;

    gst_init (nullptr, nullptr);
    prepare();
    connect(&p->watcher, SIGNAL(finished()), this, SLOT(loadThreadFinished()));

}

void TrackAnalyser::sync_set_state(GstElement* element, GstState state)
{ GstStateChangeReturn res; \
        res = gst_element_set_state (GST_ELEMENT (element), state); \
        if(res == GST_STATE_CHANGE_FAILURE) return; \
        if(res == GST_STATE_CHANGE_ASYNC) { \
                GstState state; \
                        res = gst_element_get_state(GST_ELEMENT (element), &state, NULL, 1000000000/*GST_CLOCK_TIME_NONE*/); \
                        if(res == GST_STATE_CHANGE_FAILURE || res == GST_STATE_CHANGE_ASYNC) return; \
} }

TrackAnalyser::~TrackAnalyser()
{
    cleanup();
    delete p;
    p = nullptr;
}


void cb_newpad_ta (GstElement *src,
                   GstPad     *new_pad,
                   gpointer    data)
{
    TrackAnalyser* instance = (TrackAnalyser*)data;
            instance->newpad(src, new_pad, data);
}


void TrackAnalyser::newpad (GstElement *src,
                   GstPad     *new_pad,
                   gpointer    data)
{
        GstCaps *caps;
        GstStructure *str;
        GstPad *sink_pad;

        /* only link once */
        GstElement *bin = gst_bin_get_by_name(GST_BIN(pipeline), "convert");
        sink_pad = gst_element_get_static_pad (bin, "sink");

        if (GST_PAD_IS_LINKED (sink_pad)) {
                g_object_unref (sink_pad);
                return;
        }

        /* check media type */
#ifdef GST_API_VERSION_1
        caps = gst_pad_query_caps (new_pad, nullptr);
#else
        caps = gst_pad_get_caps (new_pad);
#endif
        str = gst_caps_get_structure (caps, 0);
        if (!g_strrstr (gst_structure_get_name (str), "audio")) {
                gst_caps_unref (caps);
                gst_object_unref (sink_pad);
                return;
        }
        gst_caps_unref (caps);

        /* link'n'play */
        gst_pad_link (new_pad, sink_pad);
}

GstBusSyncReply TrackAnalyser::bus_cb (GstBus *bus, GstMessage *msg, gpointer data)
{
    TrackAnalyser* instance = (TrackAnalyser*)data;
            instance->messageReceived(msg);
    return GST_BUS_PASS;
}

void TrackAnalyser::cleanup()
{
        if(pipeline) sync_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);
        if(bus) gst_object_unref (bus);
        if(pipeline) gst_object_unref(G_OBJECT(pipeline));
}

bool TrackAnalyser::prepare()
{
        pipeline = gst_pipeline_new ("pipeline");
        bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
        p->src = gst_element_factory_make ("uridecodebin", "source");

        g_signal_connect (p->src, "pad-added", G_CALLBACK (cb_newpad_ta), this);

        p->conv = gst_element_factory_make ("audioconvert", "convert");
        p->spectrum = gst_element_factory_make ("spectrum", "spectrum");
        p->analysis = gst_element_factory_make ("rganalysis", "analysis");
        p->cutter = gst_element_factory_make ("cutter", "cutter");
        p->sink = gst_element_factory_make ("fakesink", "sink");

        g_object_set (p->analysis, "message", TRUE, NULL);
        g_object_set (p->analysis, "num-tracks", 1, NULL);
        g_object_set (p->cutter, "threshold-dB", -25.0, NULL);

        g_object_set (G_OBJECT (p->spectrum), "bands", spect_bands, "threshold", -80,
              "post-messages", TRUE, "interval", GST_SECOND / p->fft_res, NULL);


        gst_bin_add_many (GST_BIN (pipeline), p->src, p->conv, p->analysis, p->cutter, p->spectrum, p->sink, NULL);
        gst_element_link (p->conv, p->analysis);
        gst_element_link (p->analysis, p->cutter);
        gst_element_link (p->cutter, p->sink);

#ifdef GST_API_VERSION_1
        gst_bus_set_sync_handler (bus, bus_cb, this, nullptr);
#else
        gst_bus_set_sync_handler (bus, bus_cb, this);
#endif

        return pipeline;
}

int TrackAnalyser::bpm()
{
    return  p->bpm;
}

double TrackAnalyser::gainDB()
{
    return  m_GainDB;
}

double TrackAnalyser::gainFactor()
{
    return pow (10, m_GainDB / 20);
}

QTime TrackAnalyser::startPosition()
{
    return m_StartPosition;
}

QTime TrackAnalyser::endPosition()
{
    return m_EndPosition;
}

void TrackAnalyser::setPosition(QTime position)
{
        int time_milliseconds=QTime(0,0).msecsTo(position);
        gint64 time_nanoseconds=( time_milliseconds * GST_MSECOND );
        gst_element_seek (pipeline, 1.0, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH,
                                 GST_SEEK_TYPE_SET, time_nanoseconds,
                                 GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);
        qDebug() << Q_FUNC_INFO <<":"<<" position="<<position;
}

void TrackAnalyser::setMode(modeType mode)
{
    p->analysisMode = mode;
    sync_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);

    //divide in multiple analyser due to different running times
    switch (p->analysisMode)
    {
        case TEMPO:
        gst_element_unlink (p->conv, p->analysis);
        gst_element_unlink (p->analysis, p->cutter);
        gst_element_unlink (p->cutter, p->sink);

        gst_element_link (p->conv, p->spectrum);//spectrum take too much time
        gst_element_link (p->spectrum, p->sink);
        break;
    default:
        gst_element_unlink (p->conv, p->spectrum);
        gst_element_unlink (p->spectrum, p->sink);

        gst_element_link (p->conv, p->analysis);
        gst_element_link (p->analysis, p->cutter);
        gst_element_link (p->cutter, p->sink);
        m_StartPosition = m_MaxPosition = QTime(0,0);
    }
}

void TrackAnalyser::open(QUrl url)
{
    //To avoid delays load track in another thread
    qDebug() << Q_FUNC_INFO <<":"<<parentWidget()->objectName()<<" url="<<url;
    QFuture<void> future = QtConcurrent::run( this, &TrackAnalyser::asyncOpen,url);
    p->watcher.setFuture(future);
}

void TrackAnalyser::asyncOpen(QUrl url)
{
    p->mutex.lock();
    m_GainDB = GAIN_INVALID;
    //m_StartPosition = QTime(0,0);
    p->spectralFlux.clear();

    sync_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);


    GstElement *src = gst_bin_get_by_name(GST_BIN(pipeline), "source");
    g_object_set (G_OBJECT (src), "uri", (const char*)url.toString().toUtf8(), nullptr);

    sync_set_state (GST_ELEMENT (pipeline), GST_STATE_PAUSED);

    m_finished=false;

    gst_object_unref(src);
    p->mutex.unlock();
}

void TrackAnalyser::loadThreadFinished()
{
    // async load in player done
    qDebug() << Q_FUNC_INFO <<":"<<parentWidget()->objectName()<<" analysisMode="<<p->analysisMode;

    if ( p->analysisMode == TrackAnalyser::TEMPO ){
        //setPosition( m_EndPosition.addSecs(-SCAN_DURATION) );
        setPosition(m_StartPosition);
    }
    else {
        m_EndPosition=length();
    }
    start();
}

void TrackAnalyser::start()
{
    qDebug() << Q_FUNC_INFO <<":"<<parentWidget()->objectName();
    gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);
}


bool TrackAnalyser::close()
{
    gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);
    return true;
}


QTime TrackAnalyser::length()
{
    if (pipeline) {

        gint64 value=0;

#ifdef GST_API_VERSION_1
        if(gst_element_query_duration(pipeline, GST_FORMAT_TIME, &value)) {
#else
        GstFormat fmt = GST_FORMAT_TIME;
        if(gst_element_query_duration(pipeline, &fmt, &value)) {
#endif
            //qDebug() << Q_FUNC_INFO <<  " new value:" <<value;
            m_MaxPosition = QTime(0,0).addMSecs( static_cast<uint>( ( value / GST_MSECOND ) )); // nanosec -> msec
        }
    }
    //qDebug() << Q_FUNC_INFO <<  " return:" <<m_MaxPosition;
    return m_MaxPosition;
}


void TrackAnalyser::messageReceived(GstMessage *message)
{
        switch (GST_MESSAGE_TYPE (message)) {
        case GST_MESSAGE_ERROR: {
                GError *err;
                gchar *debug;
                gst_message_parse_error (message, &err, &debug);
                QString str;
                str = "Error #"+QString::number(err->code)+" in module "+QString::number(err->domain)+"\n"+QString::fromUtf8(err->message);
                if(err->code == 6 && err->domain == 851) {
                        str += "\nMay be you should to install gstreamer0.10-plugins-ugly or gstreamer0.10-plugins-bad";
                }
                qDebug()<< "Gstreamer error:"<< str;
                g_error_free (err);
                g_free (debug);
                need_finish();
                break;
        }
        case GST_MESSAGE_EOS:{
                qDebug() << Q_FUNC_INFO <<":"<<parentWidget()->objectName()<<" End of track reached";
                need_finish();
                break;
        }
        case GST_MESSAGE_ELEMENT:{

                const GstStructure *s = gst_message_get_structure (message);
                const gchar *name = gst_structure_get_name (s);
                GstClockTime timestamp;
                gst_structure_get_clock_time (s, "timestamp", &timestamp);

                // data for tempo detection
                if (strcmp (name, "spectrum") == 0) {
                  const GValue *magnitudes;
                  const GValue *mag;
                  float mag_value;
                  guint i;

                  magnitudes = gst_structure_get_value (s, "magnitude");

                  float flux = 0;
                  for (i = 0; i < spect_bands; ++i) {
                    //gdouble freq = (gdouble) ((AUDIOFREQ / 2) * i + AUDIOFREQ / 4) / spect_bands;
                    mag = gst_value_list_get_value (magnitudes, i);
                    mag_value = pow (10.0, g_value_get_float (mag)/ 20.0);
                    float value = (mag_value - p->lastSpectrum[i]);
                    p->lastSpectrum[i] = mag_value;
                    flux += value < 0? 0: value;
                    //qDebug() << Q_FUNC_INFO <<"freq:"<<freq<<" flux:"<<flux;
                  }
                  //Spectral flux (comparing the power spectrum for one frame against the previous frame)
                  //for onset detection
                  p->spectralFlux.append( flux );

                }
                // data for Start and End time detection
                if (strcmp (name, "cutter") == 0) {

                    const GValue *value;
                    value=gst_structure_get_value (s, "above");
                    bool isSilent=!g_value_get_boolean(value);

                    //if we detect a falling edge, set EndPostion to this
                    if (isSilent)
                        m_EndPosition=QTime(0,0).addMSecs( static_cast<uint>( ( timestamp / GST_MSECOND ) )); // nanosec -> msec
                    else
                    {
                        //if this is the first rising edge, set StartPosition
                        if (m_StartPosition==QTime(0,0) && m_EndPosition==m_MaxPosition)
                            m_StartPosition=QTime(0,0).addMSecs( static_cast<uint>( ( timestamp / GST_MSECOND ) )); // nanosec -> msec

                        //if we detect a rising edge, set EndPostion to track end
                        m_EndPosition=length();
                    }
                    //qDebug() << Q_FUNC_INFO <<QTime(0,0).addMSecs( static_cast<uint>( ( timestamp / GST_MSECOND ) ))<< " silent:" << isSilent;
                }
            break;
          }

        case GST_MESSAGE_TAG:{

                GstTagList *tags = NULL;
                gst_message_parse_tag (message, &tags);
                if (gst_tag_list_get_double (tags, GST_TAG_TRACK_GAIN, &m_GainDB))
                {
                    qDebug() << Q_FUNC_INFO << ": Gain-db:" << m_GainDB;
                    qDebug() << Q_FUNC_INFO << ": Gain-norm:" << pow (10, m_GainDB / 20);
                }
            }

        default:
                break;
        }

}

void TrackAnalyser::need_finish()
{
    m_finished=true;
    switch (p->analysisMode)
    {
        case TEMPO:
            detectTempo();
            Q_EMIT finishTempo();
            break;
        default:
            Q_EMIT finishGain();
    }
}

void TrackAnalyser::detectTempo()
{
    int THRESHOLD_WINDOW_SIZE = 10;
    float MULTIPLIER = 1.5f;
    QList<float> prunedSpectralFlux;
    QList<float> threshold;
    QList<float> peaks;

    //calculate the running average for spectral flux.
    for( int i = 0; i < p->spectralFlux.size(); i++ )
    {
       int start = qMax( 0, i - THRESHOLD_WINDOW_SIZE );
       int end = qMin( p->spectralFlux.size() - 1, i + THRESHOLD_WINDOW_SIZE );
       float mean = 0;
       for( int j = start; j <= end; j++ )
          mean += p->spectralFlux.at(j);
       mean /= (end - start);
       threshold.append( mean * MULTIPLIER );
    }

    //take only the signifikat onsets above threshold
    for( int i = 0; i < threshold.size(); i++ )
    {
       if( threshold.at(i) <= p->spectralFlux.at(i) )
          prunedSpectralFlux.append( p->spectralFlux.at(i) - threshold.at(i) );
       else
          prunedSpectralFlux.append( (float)0 );
    }

    //peak detection
    for( int i = 0; i < prunedSpectralFlux.size() - 1; i++ )
    {
       if( prunedSpectralFlux.at(i) > prunedSpectralFlux.at(i+1) )
          peaks.append( prunedSpectralFlux.at(i) );
       else
          peaks.append( (float)0 );
    }

    //use autocorrelation to retrieve time periode of peaks
    float bpm = AutoCorrelation(peaks, peaks.count(), 60, 240, p->fft_res);
    qDebug() << Q_FUNC_INFO << "autocorrelation bpm:"<<bpm;

    //tempo-harmonics issue
    //if ( bpm < 72.0 ) {
    //    bpm *= 2;
    //    qDebug() << Q_FUNC_INFO << "guess bpm:"<<bpm;
    //} //We dont care about tempo-harmonics issue -> music fits anyway -> factor: 2x or 0.5x
    p->bpm = qRound(bpm);
}

float TrackAnalyser::AutoCorrelation( QList<float> buffer, int frames, int minBpm, int maxBpm, int sampleRate)
{

    float maxCorr = 0;
    int maxLag = 0;
    float std_bpm = 120.0f;
    float std_dev = 0.8f;

    int maxOffset = sampleRate * 60 / minBpm;
    int minOffset = sampleRate * 60 / maxBpm;
    if (frames > buffer.count()) frames=buffer.count();

    for (int lag = minOffset; lag < maxOffset; lag++)
    {
        float corr = 0;
        for (int i = 0; i < frames-lag; i++)
        {
            corr += (buffer.at(i+lag) * buffer.at(i));
        }

        //float bpm = sampleRate * 60.0 / lag;

        //calculate rating according then common bpm of 120 (log normal distribution)
        //float rate = (float) qExp( -0.5 * qPow(( log( bpm / std_bpm ) / log(2) / std_dev),2.0));
        //corr = corr * rate;
        //We dont care about tempo-harmonics issue -> music fits anyway -> factor: 2x or 0.5x

        if (corr > maxCorr)
        {
            //qDebug() << Q_FUNC_INFO << "corr: "<<corr<<" bpm:"<<bpm;
            maxCorr = corr;
            maxLag = lag;
        }

    }
    if (maxLag>0)
        return sampleRate * 60.0 / maxLag;
    else
        return 0.0;
}
